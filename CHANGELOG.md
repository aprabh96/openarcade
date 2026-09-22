# Changelog

## 1.0.0 (unreleased)

First public release. Rebuilt from the system that ran the VR Lawrence arcade (7 stations,
2021-2025), with every venue-specific value moved into settings.

- Customer booking page with server-side availability, station allocation and pricing.
- Staff dashboard: day timeline per station, walk-ins, reschedule, cancel, session timers,
  station commands, settings for hours, prices, closures, stations and branding.
- No double booking under concurrency, proven by a multi-process test.
- Optional Square card payments with holds, idempotent charges, refunds and reconciliation.
- Optional Pusher station commands (`START_SESSION`, `STOP_SESSION`, `ADD_TIME`).
- Email confirmations through SMTP, PHP `mail()` or a log file.
- `bin/console`: install, migrate, admin:create, seed:demo, holds:release, privacy:purge, doctor.
- `setup.php` one-time installer for shared hosting; `doctor --online` go-live check.
- Copy-paste prompts for AI agents under `docs/agent-prompts/`.
