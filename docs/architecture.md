# Architecture

One install per venue. PHP 8.1+ with `strict_types` everywhere, PDO against MySQL or MariaDB, no
framework, no front-end build. The runtime dependency is PHPMailer; Square and Pusher are called
over plain HTTPS through one small `HttpClient` interface so they can be faked in tests.

## Layers

```
public/           web root: index.php front controller, static pages and scripts
src/Http/         Router, Request, Response, App (error mapping, security headers), PublicApi, AdminApi, RateLimiter
src/Auth/         AdminAuth (sessions), Csrf, BookingToken, LoginThrottle
src/Domain/       Availability, StationAllocator, Reservations (the write path), BookingFlow (booking + payment + email)
src/Settings/     VenueSettings (validated) and its repository
src/Payments/     PaymentGateway: NullGateway, SquareGateway
src/Realtime/     Notifier: NullNotifier, PusherNotifier
src/Mail/         Mailer: LogMailer, PhpMailerMailer; ReservationMail templates
src/Db/           Connection (UTC session), Migrator, Transaction (deadlock retry)
src/Console/      Application (commands), Installer, Doctor
```

Controllers validate input and call domain services. Domain services never read the request or
echo; they receive their collaborators (clock, repositories, gateway, notifier, mailer) through
constructors, which is why the whole API can be tested in-process with a fixed clock.

## The no-double-booking guarantee

Two customers can never be given the same station for overlapping times, even under concurrent
requests or direct API calls:

1. `Reservations::create()` first runs an autocommit `INSERT IGNORE` into `booking_days` for the
   date (outside any transaction, so no shared lock lingers).
2. Inside the transaction the first statement is `SELECT ... FOR UPDATE` on that row. Every writer
   for the same date queues behind it. Because the read view is established after the lock is
   acquired, the writer sees every earlier commit.
3. Under the lock it reloads the day's blocking sessions (confirmed, plus holds that have not
   expired), recomputes free stations with the buffer rule, and lets `StationAllocator` pick
   stations (tightest fit first, then lowest station number).
4. The price is computed on the server from the price table. The client never sends a price.
5. Insert and commit. Reschedules lock both the old and the new date in sorted order, so two staff
   edits cannot deadlock.

`tests/Integration/ConcurrencyTest.php` starts eight real PHP processes racing for the same station
and asserts exactly one wins; disabling the lock makes every run fail.

## Payment flow

With `PAYMENT_MODE=square`, a booking is inserted as `held` with a deadline (`hold_minutes`). The
server charges Square with the server-side amount, the reservation uuid as idempotency key and
the confirmation code as `reference_id`. Paid: `confirmed`. Declined: `payment_failed`, stations
free again at once. No answer: the hold stays until `holds:release` (cron, every 5 minutes) asks
Square whether a payment with that reference exists; found and stations still free: confirmed;
found but stations taken: refunded; not found: expired. A late payment is handled the same way
whether or not the sweep already ran.

## Data model

Money is integer cents. Instants are UTC `DATETIME`s; `local_date` is stored alongside for day
queries and locking; times of day travel as minutes after local midnight. Wall-clock times are
converted with `setTime()` so daylight-saving change days stay correct. Tables: `settings`,
`stations`, `business_hours`, `special_hours`, `closed_dates`, `prices`, `booking_days`,
`reservations`, `reservation_stations`, `admins`, `login_attempts`, `rate_limits`, `migrations`.

## Security model

- Admin passwords hashed with `password_hash`, minimum 12 characters, sign-in throttled per
  username and per client, session id regenerated on login, idle timeout, `Secure`/`HttpOnly`/`SameSite=Lax` cookies.
- CSRF token on every admin write; public bookings need a session-bound signed booking token plus
  a same-origin `Origin`/`Referer`; rate limits per client on booking, token and login.
- Prepared statements everywhere; UIs insert text with `textContent`; CSP restricts scripts to the
  site and Square's CDN and framing to the configured origins.
- Logs never contain names, emails, phones, tokens or card data; IP addresses are stored only as
  keyed hashes; `privacy:purge` anonymises old reservations.
- Private folders are denied by `.htaccess` when the document root cannot be `public/`, and
  `doctor --online` verifies it from outside.
