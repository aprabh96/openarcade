# Deploy on shared hosting

Requirements: PHP 8.1 or newer with `pdo_mysql`, `curl`, `mbstring` and `openssl`; a MySQL 5.7+ or
MariaDB 10.4+ database; Apache with `mod_rewrite` (the usual cPanel, Plesk and DirectAdmin setup).
No shell needed: the release zip includes `vendor/`, and `setup.php` replaces the console for the
first install.

1. **Database.** Create a database and a user with all privileges on it in the hosting panel.
2. **Files.** Unpack the release zip. Ideally set the document root of your (sub)domain to the
   `public` folder. If the host cannot do that, upload everything into the web root as it is: the
   root `.htaccess` denies access to every folder, and `public/.htaccess` re-allows only the web
   root, so `src/`, `storage/`, `migrations/`, `vendor/` and `.env` stay private. Either way,
   `storage/logs` and `storage/cache` must be writable by PHP.
3. **Configuration.** Copy `.env.example` to `.env` next to `composer.json` and set:
   `APP_ENV=production`, `APP_DEBUG=false`, `APP_URL=https://your-domain`, a random 64-character
   `APP_KEY`, the `DB_*` values, and a second random 64-character `SETUP_TOKEN`.
4. **Install.** Open `https://your-domain/setup.php`, paste the `SETUP_TOKEN` value into the form (it
   is never put in the URL, so it stays out of logs), then fill in the venue name,
   timezone, station count and the first admin account, submit once. Then remove `SETUP_TOKEN` from
   `.env`; the page refuses to run again anyway once an admin exists.
5. **Cron.** In the panel add: every 5 minutes `php /home/<account>/booking/bin/console holds:release`
   and monthly `php /home/<account>/booking/bin/console privacy:purge --older-than-months=12`. Use the
   PHP 8 binary your host documents (often `/usr/local/bin/php` or `ea-php82`).
6. **Verify.** `https://your-domain/book/` loads, `/admin/` shows the sign-in page, and
   `/.env`, `/src/Http/App.php`, `/storage/logs/app.log` return 403 or 404. These protections are
   Apache `.htaccess` rules; on an nginx-only host the document root must be `public/`. If the host offers a
   terminal, `php bin/console doctor --online` checks all of this for you.

Email: most shared hosts allow PHP `mail()`, so `MAIL_DRIVER=mail` with `MAIL_FROM_ADDRESS` at your
domain usually works. For reliable delivery use `MAIL_DRIVER=smtp` with the host's SMTP account.

Upgrade: upload the new zip over the old files, keeping `.env` and `storage/`, then run
`php bin/console migrate` from the terminal, or add it as a one-off cron job and remove the job after
it has run once.
