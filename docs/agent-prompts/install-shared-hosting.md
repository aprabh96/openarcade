# Prompt: install on shared hosting

Use for cPanel, Plesk, DirectAdmin or any host that gives you PHP 8.1+, MySQL and a file manager or
FTP but no shell. The agent needs access to the hosting control panel (for example through a
browser it controls) or FTP credentials that you enter yourself.

```
Install the VR arcade booking system from the release zip <path or URL of vr-arcade-os-x.y.z.zip> on my shared hosting account.

Read AGENTS.md and docs/deploy-shared-hosting.md inside the zip first, then:
1. In the hosting panel create a MySQL database and a user with all privileges on it. Do not tell me the password in chat; put it straight into .env in step 3.
2. Upload the zip contents so that the "public" folder becomes the web root of <https://booking.example.com> (a subdomain or a folder). If the host cannot change the document root, upload everything into the web root as is: the included .htaccess files keep src/, storage/ and .env private. Verify that afterwards.
3. Create .env from .env.example with APP_ENV=production, APP_DEBUG=false, APP_URL=<https://booking.example.com>, a 64-character random APP_KEY, the DB_* values, and SETUP_TOKEN set to a different 64-character random value.
4. Make storage/logs and storage/cache writable by the web server.
5. Open <https://booking.example.com>/setup.php?token=<the SETUP_TOKEN> in a browser, fill in the venue name "<Venue name>", timezone <America/Chicago>, <number> stations, admin username <owner>, and let me type the admin password. Submit once.
6. Remove SETUP_TOKEN from .env.
7. In the hosting panel add two cron jobs: every 5 minutes "php <full path>/bin/console holds:release" and monthly "php <full path>/bin/console privacy:purge --older-than-months=12". Use the PHP 8 binary the host provides.
8. Verify from outside: <https://booking.example.com>/book/ loads, /admin/ shows the sign-in page, and /.env, /src/Http/App.php and /storage/logs/app.log all return 403 or 404. If the host has a terminal, also run php bin/console doctor --online.

Rules: never print or log the database password, APP_KEY or SETUP_TOKEN; never edit files under src/ or public/ except .env; if the host lacks PHP 8.1, pdo_mysql, curl or mbstring, stop and tell me. When done, list the URLs and what is still on me.
```
