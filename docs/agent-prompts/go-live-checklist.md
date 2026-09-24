# Prompt: go-live checklist

```
Check that my booking system at <https://booking.example.com> is ready for real customers. Work through this list, fix what you can, and report the rest:

1. php bin/console doctor --online exits 0. Show me every WARN line and say whether it matters for me.
2. .env: APP_ENV=production, APP_DEBUG=false, APP_URL is https, SETUP_TOKEN is removed, TRUST_PROXY is only true if a reverse proxy is in front.
3. Email: MAIL_DRIVER is smtp or mail with a real MAIL_FROM_ADDRESS. Make a test booking with my own email <me@example.com> and confirm the confirmation email arrives, then cancel that booking in /admin/.
4. Payments (if PAYMENT_MODE=square): SQUARE_ENV=production and doctor's payment.square.credentials passes. Make a $1 test booking with a real card only if I say so, then refund it in Square.
5. Cron: holds:release every 5 minutes and privacy:purge monthly are installed and have run at least once: run php bin/console holds:release by hand and check it prints a "Holds:" line, and check the cron output (storage/logs/cron.log in the Docker guide).
6. Backups: the database is backed up daily by the host or by a cron job using mysqldump. Tell me where the backups go.
7. Hours, prices, stations, closures and the notification email are what I told you in the configuration step. Open /book/ as a customer and confirm one weekday and one weekend day look right.
8. Make a walk-in booking in /admin/, move it to another time, then cancel it.
9. Private files are not reachable: /.env, /src/, /storage/, /migrations/ return 403 or 404.
10. Tell me the two URLs to bookmark and the admin username. Never tell me the password back.
```
