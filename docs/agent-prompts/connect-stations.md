# Prompt: make the stations react to the dashboard

With a real-time driver, pressing Start, +5 min or Stop in the dashboard sends a command to the
matching station computer, which can launch the session timer, show a countdown in the headset
or lock the station when time is up. The message format is in `docs/realtime-contract.md`.

```
Connect my booking system at <https://booking.example.com> to Pusher Channels so the dashboard can send START_SESSION, STOP_SESSION and ADD_TIME to the station computers.

1. I will create a free app at https://dashboard.pusher.com (cluster <us2>) and put its app id, key and secret into .env as PUSHER_APP_ID, PUSHER_KEY, PUSHER_SECRET and PUSHER_CLUSTER. Do not ask me to paste the secret into chat.
2. Set REALTIME_DRIVER=pusher in .env and restart. Run php bin/console doctor and confirm realtime.driver passes.
3. Read docs/realtime-contract.md. Build a small station client for <Windows / Linux / Raspberry Pi> in <language> that subscribes to channel "vr-stations", listens for the event "station-<N>" where N is its station number, and on START_SESSION starts a countdown of the given minutes, on ADD_TIME adds minutes, on STOP_SESSION ends it. Show the remaining time on screen. Use the Pusher key (public) only; the secret stays on the server.
4. Test from the dashboard: open a booking on today's date, press Start, and confirm the station shows the countdown. Then +5 min, then Stop.

Report what you built and where its config file lives.
```
