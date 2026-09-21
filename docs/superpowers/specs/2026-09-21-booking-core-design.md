# Booking Core: design

Date: 2026-09-21. Status: approved by Prabh in chat (option A: harden and restructure the existing PHP/MySQL system). Working project name: `vr-arcade-os` (placeholder, easy to change before publishing).

## 1. Purpose

A self-hosted reservation system that a VR arcade (or any venue that rents numbered stations by the hour) can install, configure and run without a developer, with an AI coding agent doing the setup from copy-paste prompts. It is derived from the system that ran VR Lawrence, a 7-station arcade, from 2021 to 2025.

Success means:
- A venue owner with $5 shared hosting, or Docker, reaches a working booking page and staff dashboard by pasting one prompt into an AI agent and answering its questions.
- Two customers can never be given the same station for overlapping times, even under concurrent requests or direct API calls.
- No customer data is readable without an admin login. No secret lives in source code.
- An engineer reading the repository finds tests, clear boundaries and no embarrassing shortcuts.

## 2. Scope

In scope (this spec): public booking page, availability and pricing engine, reservations with optional Square payment, staff dashboard, admin authentication, venue settings, optional real-time station commands, confirmation email, installer and `doctor` self-check, agent setup kit, tests, CI, documentation.

Separate later specs: Station Kit (C++ station client and master controller), game menu generator, landing page and hosted demo.

Out of scope for v1: multi-venue SaaS, customer accounts, waivers, memberships, gift cards, refunds from the dashboard (use the Square dashboard), Stripe (the payment interface leaves room), mobile apps, languages other than English.

## 3. Source material and what is reused

Legacy production copy (private, never copied wholesale): `api.php` (853 lines), `bookingAdmin.php` (6,427 lines), `book_STOP.php`, `pusher_helper.php`, `sendEmail.php`, `login_handler.php`, `database.sql`.

Reused as behaviour, re-implemented as new code: the pricing model (price per duration with optional weekday override), closed weekdays, closed dates, special hours, buffer time between sessions, the interval-overlap availability idea, the station timeline dashboard, session timers, the Pusher event contract (`vr-stations` channel, `station-{n}` event, payload `station`, `command`, `value`, `timestamp`) so existing station clients keep working.

Never carried over: git history, credentials, database names, logs, customer data, real email addresses or phone numbers, Steam artwork, the WordPress site, `composer.phar`, the vendored Square SDK.

## 4. Architecture

One install per venue. PHP 8.1+ with `strict_types`, MySQL 5.7+ or MariaDB 10.4+ through PDO, no front-end build step.

```
public/                 web root
  index.php             front controller: routes /api/* and serves the two UIs
  .htaccess             rewrite to index.php, deny dotfiles
  book/                 booking UI (HTML, ES modules, CSS)
  admin/                staff dashboard UI (HTML, ES modules, CSS)
src/                    PSR-4 namespace ArcadeOS\
  Support/              Env, Config, Clock, Money, Json, Logger, Validator
  Db/                   Connection, Migrator
  Domain/               Hours, Pricing, Availability, StationAllocator, Reservations, ReservationRepository
  Payments/             PaymentGateway (interface), NullGateway, SquareGateway, HttpClient
  Realtime/             Notifier (interface), NullNotifier, PusherNotifier
  Mail/                 Mailer (interface), LogMailer, SmtpMailer, templates
  Auth/                 AdminAuth, Csrf, LoginThrottle, BookingToken
  Http/                 Router, Request, Response, controllers (PublicApi, AdminApi), RateLimiter, SecurityHeaders
  Console/              commands: install, migrate, admin:create, seed:demo, doctor, holds:release, privacy:purge
migrations/             numbered .sql files
bin/console             CLI entry point
storage/                logs and cache, never web-served
tests/                  Unit, Integration (real MariaDB), Api
docs/                   architecture, api, realtime contract, deployment guides, agent prompts
```

Runtime dependency: PHPMailer only (bundled in release zips so shared hosts need no Composer). Square and Pusher are called over their REST APIs with a small internal `HttpClient`, which keeps the install small and makes both easy to fake in tests. Dev dependencies: PHPUnit, PHPStan, PHP-CS-Fixer.

Each unit has one job and a narrow interface. Controllers validate input and call domain services; domain services never read `$_POST`, never echo, and receive their collaborators (clock, repository, gateway, notifier, mailer) through constructors so tests can substitute fakes.

## 5. Configuration

Secrets and environment facts live in `.env` (outside the web root): `APP_URL`, `APP_ENV`, `APP_DEBUG`, `APP_KEY` (HMAC key), `DB_*`, `PAYMENT_MODE` (`none` or `square`), `SQUARE_ENV`, `SQUARE_ACCESS_TOKEN`, `SQUARE_APPLICATION_ID`, `SQUARE_LOCATION_ID`, `REALTIME_DRIVER` (`none` or `pusher`), `PUSHER_*`, `MAIL_DRIVER` (`log`, `mail`, `smtp`), `SMTP_*`, `EMBED_ALLOWED_ORIGINS`, `SETUP_TOKEN`. A committed `.env.example` documents every key. A tiny built-in parser reads the file; real environment variables win over the file.

Venue facts live in the database so staff can change them in the dashboard: venue name, timezone, currency, tax rate (basis points), station count and labels, slot step minutes, buffer minutes, minimum lead minutes, maximum advance days, hold minutes, notification email, brand colour and logo URL.

## 6. Data model

All money is integer cents. All instants are stored in UTC; `local_date` is stored alongside for day queries and locking. Tables:

- `settings(key, value)`.
- `stations(id, number, label, active)`.
- `business_hours(weekday 0-6, open_time, close_time, closed)`.
- `special_hours(date, open_time, close_time)`, `closed_dates(date, reason)`.
- `prices(id, weekday NULL, duration_minutes, price_cents)`; a weekday row overrides the default row for that duration.
- `booking_days(local_date PRIMARY KEY)`: one row per date, used only as a lock target.
- `reservations(id, uuid, confirmation_code, status, first_name, last_name, email, phone, comments, local_date, start_utc, end_utc, duration_minutes, station_count, subtotal_cents, tax_cents, total_cents, currency, payment_provider, payment_id, hold_expires_at, timer_status, timer_end_utc, created_by, created_at, updated_at)`. `status` is one of `held`, `confirmed`, `cancelled`, `payment_failed`, `expired`.
- `reservation_stations(reservation_id, station_id)`.
- `admins(id, username, password_hash, created_at, last_login_at)`.
- `login_attempts(id, username, ip_hash, succeeded, created_at)`, `rate_limits(bucket, ip_hash, window_start, count)`. IP addresses are stored only as keyed hashes.
- `migrations(version, applied_at)`.

## 7. Availability and the no-double-booking guarantee

`Availability` is pure logic over a day's hours and the list of blocking reservations (status `confirmed`, or `held` with an unexpired hold). For a requested date, duration and station count it returns every start time on the slot grid with the number of stations free for `[start, end + buffer)`. A station is free when no blocking reservation on it overlaps that window (`existing.start < new.end + buffer` and `existing.end + buffer > new.start`). Opening hours come from `special_hours`, else `business_hours`, minus `closed_dates`; starts earlier than now plus the minimum lead time, or later than the maximum advance, are excluded.

`StationAllocator` picks stations for a booking: among free stations, prefer the ones that leave the smallest idle gap before the new session (keeps long blocks open), ties broken by lowest station number. The browser never chooses stations.

`Reservations::create` is the only write path and runs in one transaction:
1. `INSERT IGNORE` the `booking_days` row for the date, then `SELECT ... FOR UPDATE` on it. This serialises all writers for that date.
2. Load the day's blocking reservations, recompute availability, allocate stations. If not enough stations are free, roll back and return `slot_unavailable`.
3. Recompute the price server-side. The client never sends a price.
4. Insert the reservation as `held` (payment required) or `confirmed` (payment mode `none`, or an authenticated admin booking), plus its station rows. Commit.

Updates that change time, duration or station count go through the same lock and check, excluding the reservation being edited.

## 8. Payment flow (Square)

1. Browser tokenises the card with Square's Web Payments SDK and posts the token with the booking.
2. Server creates the `held` reservation as in section 7.
3. Server calls Square `POST /v2/payments` with the server-computed amount, currency, location, `idempotency_key` = reservation uuid, `reference_id` = confirmation code.
4. Success: reservation becomes `confirmed`, `payment_id` stored, email sent, real-time event emitted. Decline: reservation becomes `payment_failed` and the stations are free again at once. Timeout or 5xx: retry once with the same idempotency key; if still unknown the hold stays until it expires and the response tells the customer not to retry for a few minutes; `holds:release` reconciles by looking the payment up by idempotency key.

`PAYMENT_MODE=none` skips steps 1 and 3. A zero-total booking is possible only through the admin API.

## 9. HTTP API

Public (no login, never returns personal data):
- `GET /api/venue`: name, timezone, currency, tax rate, durations and prices, hours, station count, payment mode, Square application and location ids, branding.
- `GET /api/availability?date=&duration=&stations=`: start times with free-station counts.
- `GET /api/booking-token`: issues a session-bound HMAC token required by the next call.
- `POST /api/reservations`: creates a booking. Rate-limited per IP hash. Validates every field (lengths, email, phone, date, duration offered, station count within range, slot on grid).

Admin (session cookie plus `X-CSRF-Token` on every non-GET):
- `POST /api/admin/login`, `POST /api/admin/logout`, `GET /api/admin/me`.
- `GET /api/admin/reservations?date=`, `POST /api/admin/reservations`, `PATCH /api/admin/reservations/{id}`, `POST /api/admin/reservations/{id}/cancel`.
- `POST /api/admin/reservations/{id}/timer` (start, stop, extend).
- `POST /api/admin/stations/{number}/command`.
- `GET`/`PUT /api/admin/settings`, `/hours`, `/prices`, `/closures`, `/stations`.

Errors are JSON `{ "error": { "code", "message" } }` with stable codes (`validation_failed`, `slot_unavailable`, `payment_declined`, `payment_unknown`, `unauthenticated`, `forbidden`, `rate_limited`, `not_found`, `server_error`). Internal details go to the log, never to the client.

## 10. Security model

- Passwords: `password_hash` default algorithm, minimum 12 characters, login throttled per username and IP hash, session id regenerated on login, idle timeout.
- Cookies: `Secure`, `HttpOnly`, `SameSite=Lax`.
- CSRF: per-session token required on all admin writes; public booking requires the booking token and a same-origin `Origin` or `Referer`.
- Headers: CSP limited to self plus Square's SDK origins, `frame-ancestors` from `EMBED_ALLOWED_ORIGINS` so venues can embed the booking page, `X-Content-Type-Options`, `Referrer-Policy`.
- Every query uses prepared statements. Output is JSON or static files; the UIs insert text with `textContent`, never `innerHTML` with data.
- Logs never contain names, emails, phones, tokens or card data; they reference reservations by id.
- `privacy:purge --older-than=<months>` anonymises old reservations.
- When the app cannot sit outside the web root (some shared hosts), `.htaccess` files deny `src/`, `migrations/`, `storage/`, `bin/`, `vendor/`, `tests/`, `docs/` and dotfiles, and `doctor` verifies it.

## 11. Real-time station commands

`Notifier::stationCommand(int station, string command, string|int value)`. `PusherNotifier` signs and posts to Pusher's REST API on channel `vr-stations`, event `station-{n}`, with the legacy payload, so existing station clients work unchanged. The command set is exactly the legacy one, and the API rejects anything else: `START_SESSION` (value = minutes, may be fractional), `STOP_SESSION` (value = 0), `ADD_TIME` (value = minutes). `docs/realtime-contract.md` documents it. With `REALTIME_DRIVER=none` the dashboard hides station controls.

## 12. Email

On confirmation: one email to the customer (date, time, duration, stations, total, confirmation code, venue contact) and one to the venue notification address. Templates are escaped PHP files. Drivers: `log` (writes to storage, default in development), `mail`, `smtp` (PHPMailer). A mail failure never fails the booking; it is logged and surfaced in the dashboard.

## 13. User interfaces

Both are plain HTML, CSS and ES modules. Branding through CSS variables set from `/api/venue`.

- Booking page: pick date, duration and station count; see available times; enter contact details; pay if required; confirmation screen. Works inside an iframe. Keyboard and screen-reader usable.
- Staff dashboard: login; day view with one lane per station, current-time marker, reservation cards; create, edit, move and cancel; session timers with start, stop and extend; station command buttons; settings pages for venue, hours, prices, closures and stations.

## 14. Install, doctor and the agent kit

- `bin/console install` runs migrations, seeds default hours and prices, creates the first admin. `seed:demo` adds fake reservations for demos and screenshots.
- Shared hosting without SSH: `public/setup.php` works only while `SETUP_TOKEN` is set and no admin exists, then refuses to run.
- `bin/console doctor [--json]` checks PHP version and extensions, `.env` completeness, that `.env` and non-public folders are not reachable over HTTP, database connection and migration state, admin exists, debug off, HTTPS, cookie flags, payment and real-time and mail configuration, at least one price and open day, leftover `SETUP_TOKEN`. Exit code is non-zero on any failure, so an agent can loop until it passes.
- `AGENTS.md` plus `docs/agent-prompts/`: install with Docker, install on shared hosting, configure the venue, connect Square (sandbox then live), embed on WordPress, Wix or Squarespace, go-live checklist, upgrade. Each prompt lists what to ask the owner, exact steps, the doctor checks that prove success, and hard rules (never print secrets, never disable a check, never touch customer data).

## 15. Testing and quality gates

- Unit: Money, Hours, Pricing, Availability (overlap, buffer, slot grid, DST changes, lead and advance limits), StationAllocator, Validator, BookingToken, Csrf.
- Integration against MariaDB in Docker: migrations, reservation creation, update and cancel, hold expiry, and a race test that forks concurrent writers for the last free station and asserts exactly one wins.
- API: every endpoint for auth, validation and error codes; public endpoints proven to leak no personal data; admin endpoints proven to refuse anonymous and CSRF-less requests.
- Payments: `SquareGateway` against a fake `HttpClient` for success, decline, timeout and idempotent retry.
- Static analysis (PHPStan level 6), coding standard (PSR-12), `composer audit`.
- Clean-repo gate (`bin/check-clean`, also run in CI with gitleaks): fails on key patterns, non-example email addresses, phone-like strings, and SHA-256 matches against a denylist of the legacy secrets and identifiers (hashes only, never the values).
- CI on GitHub Actions runs all of the above on every push.

## 16. Delivery

Built in `E:\vr-arcade-os` with fresh git history and demo data only. Stages: scaffold and gates; config, migrations and console; domain engine; reservations with locking; HTTP and public API; auth and admin API; payments; real-time; mail; booking UI; dashboard UI; doctor, setup page and agent kit; hardening review and documentation. Nothing is pushed or made public until Prabh has reviewed it. License MIT, with third-party notices.
