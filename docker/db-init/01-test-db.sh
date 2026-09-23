#!/bin/bash
# Runs once when the db volume is first created: adds the test database next to the app database
# and grants the app user access to it. Uses the same MARIADB_* values as docker-compose.yml.
set -e
mariadb --protocol=socket -uroot <<SQL
CREATE DATABASE IF NOT EXISTS \`${MARIADB_DATABASE}_test\` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
GRANT ALL PRIVILEGES ON \`${MARIADB_DATABASE}_test\`.* TO '${MARIADB_USER}'@'%';
FLUSH PRIVILEGES;
SQL
