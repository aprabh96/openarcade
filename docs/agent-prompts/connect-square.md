# Prompt: take card payments with Square

Requires a Square account. Cards are entered in Square's own secure form; this system never sees
card numbers. Test in the sandbox first, then switch to production.

```
Connect my booking system at <https://booking.example.com> to Square so customers pay when they book.

Read docs/deploy-docker.md or docs/deploy-shared-hosting.md for where .env lives on my setup, then:
1. I will create a Square application at https://developer.squareup.com/apps myself. Tell me exactly which three values to copy: the Sandbox Access Token, the Sandbox Application ID, and the Location ID (from Locations in the Square Dashboard, or the sandbox test location). I will put them into .env as SQUARE_ACCESS_TOKEN, SQUARE_APPLICATION_ID and SQUARE_LOCATION_ID. Do not ask me to paste them into this chat.
2. Set PAYMENT_MODE=square and SQUARE_ENV=sandbox in .env. Confirm APP_URL is https; Square's card form refuses http.
3. Run php bin/console doctor --online and confirm the line payment.square.credentials passes.
4. Open /book/ and make one sandbox booking with Square's test card 4111 1111 1111 1111, any future expiry, CVV 111, postal code 94103. Confirm the booking shows as paid in /admin/ and appears in the Square sandbox dashboard.
5. Explain to me how to switch to production later: production token, production application id, real location id, SQUARE_ENV=production, then doctor --online again.

Rules: never print the access token; never change PAYMENT_MODE to square while SQUARE_ENV=production without my explicit go-ahead. Also make sure the cron job "php bin/console holds:release" runs every 5 minutes: it releases unpaid holds and completes payments whose confirmation was lost.
```
