# Real-time station commands

When `REALTIME_DRIVER=pusher`, every timer action in the dashboard (and `POST /api/admin/stations/{n}/command`)
publishes one message per station through [Pusher Channels](https://pusher.com/channels). Station
computers subscribe and react. This is exactly the contract the original VR Lawrence station client
spoke, so existing clients keep working.

## Message

| Field | Value |
| --- | --- |
| Channel | `vr-stations` |
| Event name | `station-{number}`, for example `station-3` |
| Data | JSON string: `{"station": 3, "command": "START_SESSION", "value": 60, "timestamp": "2026-09-22T15:00:00+00:00"}` |

`value` is a number of minutes (it may be fractional when staff extend by a partial amount).

## Commands

| Command | `value` | Meaning |
| --- | --- | --- |
| `START_SESSION` | minutes | Start a countdown of that many minutes now |
| `ADD_TIME` | minutes | Add minutes to the running countdown |
| `STOP_SESSION` | `0` | End the session now |

The server rejects any other command name, and never sends to a station number that is not active.

## Minimal browser or Node client

```html
<script src="https://js.pusher.com/8.4/pusher.min.js"></script>
<script>
  const STATION = 3;                       // this computer's station number
  const pusher = new Pusher('<PUSHER_KEY>', { cluster: '<PUSHER_CLUSTER>' });
  pusher.subscribe('vr-stations').bind('station-' + STATION, (data) => {
    // data.command is START_SESSION | ADD_TIME | STOP_SESSION, data.value is minutes
    console.log(data.command, data.value);
  });
</script>
```

Only the public key goes on station computers. The secret stays in the server's `.env`.

## Delivery semantics

Commands are fire-and-forget. If Pusher cannot be reached, the dashboard still saves the timer
change and shows "the station could not be reached"; the explicit station-command endpoint
returns HTTP 502. There is no acknowledgement channel; a station client should be idempotent about
repeated `START_SESSION` messages.
