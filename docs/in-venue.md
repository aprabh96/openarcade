# In the venue: master controller and station apps

The booking system decides who plays when. Inside the venue, two Windows apps run the sessions
themselves over the local network. No internet connection or cloud service is involved.

```
Front desk PC                         Each gaming PC (one per headset)
+--------------------+    TCP 12345   +------------------------------------+
| Master controller  | <------------- | Station app                        |
| start / +time /    |  LAN only      |  countdown, in-headset overlay,    |
| stop, per station  | -------------> |  game menu, launches Steam and     |
| or for a group     |  commands      |  custom games, ends at time up     |
+--------------------+                +------------------------------------+
```

1. Staff check a guest in. The booking and its stations are in the dashboard at `/admin/`.
2. In the master controller they select the stations and press Start with the minutes booked.
3. Each station shows the countdown and the game menu inside the headset. Guests pick games from
   the menu and the station launches them through SteamVR.
4. Staff can add time or stop at any moment. When the countdown reaches zero the station closes the
   running game, returns the headset to the waiting screen and reports that it is free. It does this
   on its own, even if the front desk PC is off.

Staff start sessions by hand, as on the commercial arcade platforms. Starting a session
automatically when a booking begins is a possible future addition.

## Protocol

Plain text over TCP. The master controller listens on port **12345**; every station connects to it,
keeps the connection open, and retries every 5 seconds after a drop, so stations come back by
themselves when the front desk PC restarts. A running session survives a restart of the station
app too: the end time is saved in `settings.ini` and the countdown continues.

Port 12345 is fixed. Check that nothing else on the front desk PC uses it
(`netstat -ano | findstr :12345`); if another program listens there, stations connect to it instead.

| Direction | Message | Meaning |
| --- | --- | --- |
| station to master | `STATION_NAME <name>` | Sent on connect so the master can list the station |
| station to master | `SESSION_STARTED <minutes>` | A session has started |
| station to master | `TIME_LEFT <seconds>` | Sent every second while a session runs |
| station to master | `SESSION_STOPPED` | The session ended (time up or stopped); the station is free |
| station to master | `STATION_DISCONNECTING` | Sent on orderly shutdown |
| master to station | `START_SESSION <minutes>` | Start a session of that many minutes and show the overlay |
| master to station | `ADD_TIME <minutes>` | Add minutes to the running session |
| master to station | `STOP_SESSION` | End the session now |

Minutes may be fractional: `START_SESSION 0.5` is 30 seconds.

## Security

The protocol has no password or encryption. That is acceptable on a venue's private network and
not anywhere else. Keep port 12345 closed to the internet (the default on home and business
routers), and put the gaming PCs on a network guests cannot join, such as a separate Wi-Fi or VLAN.
Anyone who can reach port 12345 on the front desk PC can pose as a station, and anyone who can run
a server at the address a station expects can send it commands.
