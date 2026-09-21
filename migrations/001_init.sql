-- Booking Core schema, version 1. All DATETIME values are UTC.

CREATE TABLE settings (
  `key` VARCHAR(64) NOT NULL PRIMARY KEY,
  `value` TEXT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE stations (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  number INT UNSIGNED NOT NULL,
  label VARCHAR(60) NOT NULL,
  active TINYINT(1) NOT NULL DEFAULT 1,
  UNIQUE KEY uq_stations_number (number)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE business_hours (
  weekday TINYINT UNSIGNED NOT NULL PRIMARY KEY COMMENT '0 = Sunday .. 6 = Saturday',
  open_minute SMALLINT UNSIGNED NOT NULL,
  close_minute SMALLINT UNSIGNED NOT NULL,
  closed TINYINT(1) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE special_hours (
  local_date DATE NOT NULL PRIMARY KEY,
  open_minute SMALLINT UNSIGNED NOT NULL,
  close_minute SMALLINT UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE closed_dates (
  local_date DATE NOT NULL PRIMARY KEY,
  reason VARCHAR(120) NOT NULL DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE prices (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  weekday TINYINT NOT NULL DEFAULT -1 COMMENT '-1 = every day, 0 = Sunday .. 6 = Saturday',
  duration_minutes INT UNSIGNED NOT NULL,
  price_cents INT UNSIGNED NOT NULL,
  UNIQUE KEY uq_prices_weekday_duration (weekday, duration_minutes)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE booking_days (
  local_date DATE NOT NULL PRIMARY KEY
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE reservations (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  uuid CHAR(36) NOT NULL,
  confirmation_code VARCHAR(12) NOT NULL,
  status VARCHAR(20) NOT NULL,
  first_name VARCHAR(60) NOT NULL,
  last_name VARCHAR(60) NOT NULL,
  email VARCHAR(190) NOT NULL,
  phone VARCHAR(30) NOT NULL,
  comments TEXT NULL,
  local_date DATE NOT NULL,
  start_utc DATETIME NOT NULL,
  end_utc DATETIME NOT NULL,
  duration_minutes INT UNSIGNED NOT NULL,
  station_count INT UNSIGNED NOT NULL,
  subtotal_cents INT UNSIGNED NOT NULL,
  tax_cents INT UNSIGNED NOT NULL,
  total_cents INT UNSIGNED NOT NULL,
  currency CHAR(3) NOT NULL,
  payment_provider VARCHAR(20) NOT NULL DEFAULT 'none',
  payment_id VARCHAR(191) NULL,
  hold_expires_at DATETIME NULL,
  timer_status VARCHAR(20) NOT NULL DEFAULT 'not_started',
  timer_end_utc DATETIME NULL,
  created_by VARCHAR(20) NOT NULL DEFAULT 'customer',
  created_at DATETIME NOT NULL,
  updated_at DATETIME NOT NULL,
  UNIQUE KEY uq_reservations_uuid (uuid),
  UNIQUE KEY uq_reservations_code (confirmation_code),
  KEY ix_reservations_date_status (local_date, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE reservation_stations (
  reservation_id INT UNSIGNED NOT NULL,
  station_id INT UNSIGNED NOT NULL,
  PRIMARY KEY (reservation_id, station_id),
  KEY ix_reservation_stations_station (station_id),
  CONSTRAINT fk_rs_reservation FOREIGN KEY (reservation_id) REFERENCES reservations (id) ON DELETE CASCADE,
  CONSTRAINT fk_rs_station FOREIGN KEY (station_id) REFERENCES stations (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE admins (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(50) NOT NULL,
  password_hash VARCHAR(255) NOT NULL,
  created_at DATETIME NOT NULL,
  last_login_at DATETIME NULL,
  UNIQUE KEY uq_admins_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE login_attempts (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(50) NOT NULL,
  ip_hash CHAR(64) NOT NULL,
  succeeded TINYINT(1) NOT NULL,
  created_at DATETIME NOT NULL,
  KEY ix_login_attempts_lookup (username, ip_hash, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE rate_limits (
  bucket VARCHAR(40) NOT NULL,
  ip_hash CHAR(64) NOT NULL,
  window_start DATETIME NOT NULL,
  hits INT UNSIGNED NOT NULL,
  PRIMARY KEY (bucket, ip_hash, window_start)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
