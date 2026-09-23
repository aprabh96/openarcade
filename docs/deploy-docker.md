# Deploy with Docker

Works on any machine with Docker: a small cloud VM, a NAS, a spare PC at the venue.

```bash
git clone https://github.com/<owner>/<repo>.git booking && cd booking
cp .env.example .env            # set APP_URL, APP_KEY and DB_PASSWORD; compose refuses to start without the last two
docker compose build
docker compose run --rm app composer install --no-dev --optimize-autoloader
docker compose up -d
docker compose run --rm -e ARCADEOS_ADMIN_PASSWORD='choose-a-long-password' app \
  php bin/console install --admin-user=owner --venue="Orbit VR" --timezone=America/Chicago --stations=7
docker compose run --rm app php bin/console doctor
```

The app listens on `127.0.0.1:8088`, so only the machine itself can reach it. Put HTTPS in front of it
with Caddy, nginx, Traefik or your provider's load balancer, set `APP_URL` to the public https address
and `TRUST_PROXY=true`, and restart (`docker compose up -d`). The proxy must pass the client address in
`X-Forwarded-For` (appended, as nginx's `$proxy_add_x_forwarded_for` and Caddy do) and set
`X-Forwarded-Proto`. Without `TRUST_PROXY`, every visitor looks like the proxy and shares one rate
limit. To expose the port directly on a trusted network instead, set `APP_BIND=0.0.0.0`.

`docker-compose.yml` reads `.env` for `APP_*`, `PAYMENT_*`, `SQUARE_*`, `REALTIME_*`, `PUSHER_*`,
`MAIL_*`, `SMTP_*`, `EMBED_ALLOWED_ORIGINS` and `DB_PASSWORD`; the database host is always the
`db` service. Data lives in the `dbdata` volume. Back it up with:

```bash
docker compose exec db sh -c 'mariadb-dump -u "$MARIADB_USER" -p"$MARIADB_PASSWORD" "$MARIADB_DATABASE"' > backup-$(date +%F).sql
```

Cron on the host (adjust the path):

```
*/5 * * * * cd /opt/booking && docker compose run --rm app php bin/console holds:release >> storage/logs/cron.log 2>&1
0 3 1 * *   cd /opt/booking && docker compose run --rm app php bin/console privacy:purge --older-than-months=12 >> storage/logs/cron.log 2>&1
```

Upgrade: `git pull`, `docker compose build`, `composer install` as above, `php bin/console migrate`,
`docker compose up -d`, `doctor --online`.
