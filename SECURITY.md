# Security

## Reporting a problem

Email prabh@psynect.ai with a description and, if possible, steps to reproduce. Please do not
open a public issue for anything that could be exploited before a fix is out. You will get an
acknowledgement within three days and a fix or a plan within two weeks for confirmed issues.

## What the design assumes

- The web server serves `public/` only, or the shipped `.htaccess` files stay in place.
- `.env` is readable by PHP and by nobody else on shared hosts.
- HTTPS terminates at the web server or a trusted proxy (`TRUST_PROXY=true` only in that case).
- Card data never touches this application: Square's browser SDK tokenises the card and the
  server only sees a one-time token.

## Built-in protections

Prepared statements for every query; CSRF tokens on admin writes; signed, session-bound booking
tokens and same-origin checks on public bookings; request ids so a retried booking is never charged
twice; per-client rate limits and layered login throttling (`admin:unlock` to recover); `password_hash` with a 12-character minimum; session regeneration and idle timeout;
`Secure`/`HttpOnly`/`SameSite=Lax` admin cookies; HSTS over HTTPS; a Content Security Policy that only allows scripts from
the site and Square's CDN; UIs that never use `innerHTML` with data; logs without personal data;
hashed IP addresses; `privacy:purge` for old reservations; and `php bin/console doctor --online`,
which verifies from outside that private files are not served.

## Supported versions

Only the newest release gets fixes. Upgrade with `docs/agent-prompts/upgrade.md`.
