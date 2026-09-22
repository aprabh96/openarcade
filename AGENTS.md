# Instructions for AI agents

This file is read by coding agents (Codex, Claude Code, Cursor, Copilot and others) that work on
or with this repository. Two kinds of agents use it: **setup agents** installing the system for a
venue, and **contributor agents** changing the code.

## What this is

A self-hosted reservation system for VR arcades and other venues that rent numbered stations by
the hour. PHP 8.1+, MySQL or MariaDB, no framework, no build step. Customers book at `/book/`,
staff run the day at `/admin/`, everything else is a JSON API under `/api/`.

## Repository map

| Path | What lives there |
| --- | --- |
| `public/` | The web root: `index.php` front controller, `book/` customer page, `admin/` staff dashboard, `assets/`, `setup.php` (one-time installer for hosts without SSH) |
| `src/Domain/` | Booking rules: availability, station allocation, the locked reservation write path, payment holds |
| `src/Http/` | Router, request/response, public and admin API controllers, rate limiting, security headers |
| `src/Auth/` | Admin sessions, CSRF, booking tokens, login throttling |
| `src/Payments/`, `src/Realtime/`, `src/Mail/` | Square, Pusher and email behind small interfaces with null/log implementations |
| `src/Console/` | `bin/console` commands: install, migrate, doctor, holds:release, privacy:purge, seed:demo |
| `migrations/` | Plain SQL, applied in order by `bin/console migrate` |
| `tests/` | PHPUnit: unit, integration (real MariaDB) and API (in-process HTTP) suites |
| `docs/` | Architecture, API, real-time contract, deployment guides, `agent-prompts/` |
| `tools/`, `bin/check-clean` | The clean-repository gate: no secrets or personal data may be committed |

## Setup agents: how to install this for a venue

1. Read `docs/agent-prompts/README.md` and pick the prompt that matches the situation (Docker,
   shared hosting, connect Square, embed, go live, upgrade).
2. Ask the venue owner only for the facts listed in the prompt. Never guess credentials, prices,
   opening hours or the timezone.
3. Finish every task by running `php bin/console doctor` (add `--online` once the site is
   reachable) and fixing every `FAIL` line. The command exits non-zero while anything fails; loop
   until it exits 0. `WARN` lines are for the owner to decide.
4. Report what you changed, what `doctor` says, and what the owner still has to do (for example
   set a real SMTP account or switch Square from sandbox to production).

Hard rules for setup agents:

- Never print, paste, log or commit secrets: `APP_KEY`, database passwords, Square tokens, Pusher
  secrets, SMTP passwords, the admin password. Refer to them by variable name.
- Never disable, weaken or skip a check to get `doctor` green. Fix the cause.
- Never touch customer data. Do not export, copy or read the `reservations` table beyond counts.
- Never put `SETUP_TOKEN` in a URL you record anywhere. Remove it from `.env` after setup.
- Do not change the code to fit a host. Configuration lives in `.env` and the dashboard.
- If a step needs the owner (a DNS change, a Square account, a payment), stop, say exactly what is
  needed and why, and continue once they confirm.

## Contributor agents: how to change the code

- Run the gate before every commit: `composer check` (style, PHPStan level 6, all tests,
  clean-repo scan, dependency audit). Docker users: `docker compose run --rm app composer check`.
- Tests are the specification. Add a failing test first, then the code. Never weaken an assertion.
- The no-double-booking guarantee lives in `src/Domain/Reservations.php` and
  `ReservationRepository::lockDay()`. Any change there must keep
  `tests/Integration/ConcurrencyTest.php` passing five runs in a row.
- Every SQL statement uses prepared statements. Every UI text node is set with `textContent`.
  Money is integer cents. Times of day are minutes after local midnight; stored instants are UTC.
- Public endpoints must never return another customer's name, email or phone. `tests/Api/PublicApiTest.php`
  asserts this; keep that property.
- Test data uses `@example.com` emails and `555-01xx` phone numbers only. The clean-repo gate rejects
  anything that looks like a real address, phone number or API key.
- Keep files small and single-purpose. Match the surrounding code's style; do not reformat unrelated code.
- Dependencies: runtime is PHPMailer only. Do not add a framework, an ORM or a front-end build.

## Local development

```
docker compose build && docker compose run --rm app composer install
docker compose run --rm app composer check
docker compose run --rm -e ARCADEOS_ADMIN_PASSWORD=local-dev-password-123 app php bin/console install --admin-user=owner
docker compose run --rm app php bin/console seed:demo
docker compose up -d      # http://localhost:8088/book/ and /admin/
```

Without Docker: PHP 8.1+ with pdo_mysql, curl, mbstring; a MariaDB/MySQL server; set the `DB_*`,
`APP_KEY`, `APP_URL` variables (or a `.env` file); `composer install`; `php -S 127.0.0.1:8088 -t public public/index.php`.
