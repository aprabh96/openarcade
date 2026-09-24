-- Sessions are run by the master controller and station apps on the local network, not by the booking server.
ALTER TABLE reservations DROP COLUMN timer_status, DROP COLUMN timer_end_utc;
