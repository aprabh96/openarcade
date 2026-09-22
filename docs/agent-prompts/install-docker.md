# Prompt: install with Docker

Use when the venue has a server, a NAS or a spare computer with Docker installed, or a cloud VM.

```
Install the VR arcade booking system from https://github.com/<owner>/<repo> on this machine with Docker.

Read AGENTS.md and docs/deploy-docker.md in the repository first, then do the following and ask me only for the facts you cannot find:
1. Clone the repository into <folder, for example /opt/booking> and copy .env.example to .env.
2. In .env set APP_ENV=production, APP_DEBUG=false, APP_URL=<https://booking.example.com>, and generate APP_KEY with: php -r "echo bin2hex(random_bytes(32));" (or openssl rand -hex 32). Set DB_HOST=db, DB_NAME=arcadeos, DB_USER=arcadeos and a strong DB_PASSWORD. Leave PAYMENT_MODE=none, REALTIME_DRIVER=none and MAIL_DRIVER=log for now.
3. Build and start: docker compose build; docker compose run --rm app composer install --no-dev --optimize-autoloader; docker compose up -d.
4. Install: docker compose run --rm -e ARCADEOS_ADMIN_PASSWORD='<I will type this when you ask>' app php bin/console install --admin-user=<owner> --venue="<Venue name>" --timezone=<America/Chicago> --stations=<number>.
5. Put the site behind HTTPS at APP_URL (Caddy, nginx or the hosting provider's proxy). If you use a reverse proxy, set TRUST_PROXY=true in .env and restart.
6. Add two cron jobs on the host: every 5 minutes "docker compose -f <folder>/docker-compose.yml run --rm app php bin/console holds:release" and monthly "... php bin/console privacy:purge --older-than-months=12".
7. Run docker compose run --rm app php bin/console doctor --online and fix every FAIL until the command exits 0. Show me the full output.

Rules: never print or log any secret or password; never disable a check; do not change any file under src/ or public/. When done, tell me the booking page URL, the dashboard URL, and what is still on me (email sending, card payments).
```

What the agent will need from you: the domain name, the venue name, timezone, number of stations,
the admin username, and the admin password typed at the prompt.
