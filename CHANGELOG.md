# Changelog

## 1.0.0 (unreleased)

First public release. Rebuilt from the system that ran the VR Lawrence arcade (7 stations,
2021-2025), with every venue-specific value moved into settings.

- Customer booking page with server-side availability, station allocation and pricing.
- Staff dashboard: day timeline per station, walk-ins, reschedule, cancel, settings for hours, prices, closures, stations and branding.
- No double booking under concurrency, proven by a multi-process test.
- Optional Square card payments with holds, idempotent charges, refunds and reconciliation.
- Pusher removed: it was never wired to the station software. Sessions run over the venue LAN.
- Email confirmations through SMTP, PHP `mail()` or a log file.
- `bin/console`: install, migrate, admin:create, seed:demo, holds:release, privacy:purge, doctor.
- `setup.php` one-time installer for shared hosting; `doctor --online` go-live check.
- Copy-paste prompts for AI agents under `docs/agent-prompts/`.
- Retried bookings carry a `request_id` and are never charged twice; failed payment lookups keep the
  hold instead of expiring it; any paid booking that cannot be confirmed is refunded; Square
  configuration errors are reported as such, not as declined cards.
- Login throttling cannot be used to lock the owner out; `admin:unlock` added.
- Docker: no default secrets, app bound to localhost, database init follows `DB_USER`.
