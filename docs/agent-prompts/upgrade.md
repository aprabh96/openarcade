# Prompt: upgrade to a new release

```
Upgrade my booking system at <folder or host> from its current version to <x.y.z> (<zip path or git tag>).

1. Read CHANGELOG.md for the new version and tell me anything that needs a decision before you start.
2. Back up first: a mysqldump of the database and a copy of .env, both to <backup folder>. Do not proceed without both.
3. Docker: git pull (or unpack the zip over the folder, keeping .env and storage/), docker compose build, docker compose run --rm app composer install --no-dev --optimize-autoloader, docker compose run --rm app php bin/console migrate, docker compose up -d.
   Shared hosting: upload the new zip contents over the old files except .env and storage/, then run "php bin/console migrate" through the host's terminal. Without a terminal, add a one-off cron job that runs it, wait for it to run, then remove the job. (setup.php only runs on a fresh install.)
4. Run php bin/console doctor --online and fix every FAIL.
5. Open /book/ and /admin/ and confirm today's bookings are still there.
6. If anything fails and cannot be fixed within a few minutes, restore the backup and tell me exactly what went wrong.
```
