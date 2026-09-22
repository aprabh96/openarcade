# Prompt: configure the venue

Everything here can also be done by hand in the dashboard under Settings. Use the prompt when you
would rather describe your venue in one message.

```
Configure my VR arcade booking system at <https://booking.example.com>. Sign in to /admin/ as <owner> (I will type the password) and set:

Venue: name "<Venue name>", timezone <America/Chicago>, currency <USD>, sales tax <9.35>% (enter it as basis points: 935), address "<street, city>", phone "<785-555-0100>", brand colour <#1f2937>, logo <https://... or none>, send new-booking emails to <bookings@example.com>.
Stations: <7>, labelled <"Station 1" ... or custom names>.
Opening hours: <Mon-Thu 12:00-21:00, Fri 12:00-23:00, Sat 10:00-23:00, Sun 12:00-20:00>.
Prices per station: <60 min $25, 90 min $35, 120 min $45>; <weekend 60 min $30>.
Booking rules: start times every <30> minutes, <10> minute gap between sessions, at least <60> minutes notice, bookable <90> days ahead, unpaid holds expire after <10> minutes.
Closures: <closed Dec 25; open 10:00-16:00 on Dec 24>.

Use the dashboard's Settings screens (or the admin API described in docs/api.md). After saving, open /book/ as a customer, check that <a Saturday> shows the weekend price and the right slots, and report what you set. Do not create test bookings unless I ask.
```
