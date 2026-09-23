-- A client-chosen id per booking attempt, so a retried submission returns the first reservation
-- instead of creating (and charging) a second one.
ALTER TABLE reservations ADD COLUMN request_id VARCHAR(64) NULL AFTER uuid;
CREATE UNIQUE INDEX uq_reservations_request_id ON reservations (request_id);
