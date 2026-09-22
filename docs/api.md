# HTTP API

All endpoints are under `/api/`, speak JSON, and return errors as
`{"error": {"code": "...", "message": "...", "details": {...}}}`. Times of day are minutes after
local midnight in the venue's timezone (600 = 10:00). Money is integer cents.

Error codes: `validation_failed` (422, `details.fields` maps field to message), `bad_request` (400),
`unauthenticated` (401), `invalid_credentials` (401), `forbidden` (403), `not_found` (404),
`method_not_allowed` (405), `slot_unavailable` (409), `wrong_status` (409), `hold_expired` (409),
`payment_declined` (402), `payment_unknown` (503), `rate_limited` / `too_many_attempts` (429),
`delivery_failed` (502), `server_error` (500). Booking-rule refusals use their rule name with 422:
`date_in_past`, `too_far_ahead`, `closed`, `outside_hours`, `off_grid`, `too_soon`,
`duration_not_offered`, `invalid_station_count`.

## Public endpoints (no login; never return personal data)

### `GET /api/venue`

Venue facts for the booking page: name, timezone, currency, tax rate, slot step, buffer, lead time,
advance limit, station count, branding, weekday hours, offered durations, price rows
(`weekday` -1 means every day), payment mode (plus Square application and location ids when
`square`), today's date and current minute.

### `GET /api/availability?date=YYYY-MM-DD&duration=60&stations=1`

Start times on the slot grid with the number of free stations for each:
`{"date", "duration_minutes", "stations", "slot_step_minutes", "closed", "open_minute", "close_minute", "slots": [{"start_minute": 600, "free": 2}]}`.
On the current date, slots before now plus the minimum lead time are omitted.

### `GET /api/booking-token`

`{"token": "..."}`. Required for `POST /api/reservations`; bound to the browser session and rotated
after every accepted booking. Rate limited.

### `POST /api/reservations`

Body: `booking_token`, `date`, `start_minute`, `duration_minutes`, `station_count`, `first_name`,
`last_name`, `email`, `phone`, optional `comments`, and `payment_token` when the venue takes card
payments (the token from Square's Web Payments SDK). Requires a same-origin `Origin` or `Referer`.
Rate limited to 10 attempts per 10 minutes per client.

Response `201`: `{"reservation": {"confirmation_code", "status", "date", "start_minute", "end_minute",
"duration_minutes", "station_count", "stations": [numbers], "subtotal_cents", "tax_cents", "total_cents",
"currency", "first_name"}}`. The server chooses the stations and computes the price; nothing sent by
the client is trusted for either.

## Admin endpoints

Sign in with `POST /api/admin/login` (`username`, `password`). The response includes `csrf_token`;
send it as the `X-CSRF-Token` header on every non-GET admin request. Sessions end on
`POST /api/admin/logout` or after `SESSION_IDLE_MINUTES` without activity. `GET /api/admin/me`
returns the current user and token. Five failed sign-ins for a username block it for 15 minutes.

| Method and path | Body | Result |
| --- | --- | --- |
| `GET /api/admin/reservations?date=` | | `{date, hours, stations, reservations: [full rows]}` |
| `POST /api/admin/reservations` | booking fields, optional `complimentary` | 201 with the row; staff bookings may be off-grid, outside hours, inside the lead time, and free of charge, but never overlap |
| `PATCH /api/admin/reservations/{id}` | any booking fields | Reschedules under the same lock; amounts are recomputed when the length or station count changes |
| `POST /api/admin/reservations/{id}/cancel` | | Frees the stations |
| `POST /api/admin/reservations/{id}/timer` | `action` start / extend / stop, optional `minutes` | `{timer, delivery: sent / disabled / failed, reservation}` and sends a station command per station |
| `POST /api/admin/stations/{number}/command` | `command`, `value` | Sends one command; 502 when delivery fails |
| `GET`/`PUT /api/admin/settings` | `{settings: {key: value}}` | Venue settings; unknown keys and invalid values come back as field errors |
| `GET`/`PUT /api/admin/hours` | `{weekdays: [{weekday, open_minute, close_minute, closed}]}` | Weekly hours (0 = Sunday) |
| `GET`/`PUT /api/admin/prices` | `{set: [{weekday, duration_minutes, price_cents}], remove: [{weekday, duration_minutes}]}` | Price rows |
| `GET`/`PUT /api/admin/closures` | `{add_closed: [{date, reason}], remove_closed: [date], add_special: [{date, open_minute, close_minute}], remove_special: [date]}` | Closed dates and special hours from today on |
| `GET`/`PUT /api/admin/stations` | `{count, labels: {"1": "Racing sim"}}` | Active stations and labels |

A reservation row: `id, uuid, confirmation_code, status (held, confirmed, cancelled, payment_failed, expired),
first_name, last_name, email, phone, comments, date, start_minute, end_minute, duration_minutes, station_count,
stations, subtotal_cents, tax_cents, total_cents, currency, payment_provider, payment_id, hold_expires_at,
timer_status (not_started, running, stopped), timer_end_utc, created_by, created_at, updated_at`.

## Security headers

Every response carries a Content Security Policy (scripts only from the site itself and Square's
CDN, framing only by `EMBED_ALLOWED_ORIGINS`), `X-Content-Type-Options: nosniff`,
`Referrer-Policy: strict-origin-when-cross-origin` and `Cache-Control: no-store`.
