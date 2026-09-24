<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

use PDO;

final class ReservationRepository
{
    private const CODE_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

    /** Columns the service is allowed to set through insert(), besides uuid and confirmation_code. */
    private const INSERTABLE = [
        'status', 'first_name', 'last_name', 'email', 'phone', 'comments', 'local_date', 'start_utc',
        'end_utc', 'duration_minutes', 'station_count', 'subtotal_cents', 'tax_cents', 'total_cents',
        'currency', 'hold_expires_at', 'created_by', 'created_at', 'updated_at', 'request_id',
    ];

    public function __construct(private PDO $pdo)
    {
    }

    /** Call OUTSIDE a transaction, so the shared lock taken by the duplicate check is released at once. */
    public function ensureDayRow(string $localDate): void
    {
        $this->pdo->prepare('INSERT IGNORE INTO booking_days (local_date) VALUES (?)')->execute([$localDate]);
    }

    /** Call INSIDE a transaction. Blocks until no other writer holds this date. */
    public function lockDay(string $localDate): void
    {
        $statement = $this->pdo->prepare('SELECT local_date FROM booking_days WHERE local_date = ? FOR UPDATE');
        $statement->execute([$localDate]);
        if ($statement->fetchColumn() === false) {
            throw new \LogicException('ensureDayRow() must be called before lockDay().');
        }
    }

    /**
     * Sessions that block stations on a date: confirmed ones, and holds that have not expired.
     *
     * @return Block[]
     */
    public function blocksForDate(string $localDate, \DateTimeImmutable $nowUtc, \DateTimeZone $tz, ?int $excludeReservationId = null): array
    {
        $sql = 'SELECT rs.station_id, r.start_utc, r.end_utc FROM reservations r '
            . 'JOIN reservation_stations rs ON rs.reservation_id = r.id '
            . "WHERE r.local_date = ? AND (r.status = 'confirmed' OR (r.status = 'held' AND r.hold_expires_at > ?))";
        $params = [$localDate, $nowUtc->format('Y-m-d H:i:s')];
        if ($excludeReservationId !== null) {
            $sql .= ' AND r.id <> ?';
            $params[] = $excludeReservationId;
        }
        $statement = $this->pdo->prepare($sql);
        $statement->execute($params);

        $blocks = [];
        foreach ($statement->fetchAll() as $row) {
            [$start, $end] = self::localMinutes((string) $row['start_utc'], (string) $row['end_utc'], $tz);
            $blocks[] = new Block((int) $row['station_id'], $start, $end);
        }

        return $blocks;
    }

    /**
     * @param array<string, int|string|null> $fields column => value, without uuid and confirmation_code
     * @param int[] $stationIds
     */
    public function insert(array $fields, array $stationIds): int
    {
        $unknown = array_diff(array_keys($fields), self::INSERTABLE);
        if ($unknown !== []) {
            throw new \InvalidArgumentException('Unknown reservation column(s): ' . implode(', ', $unknown));
        }
        $fields['uuid'] = self::uuid();
        for ($attempt = 1; ; $attempt++) {
            $fields['confirmation_code'] = self::confirmationCode();
            $columns = array_keys($fields);
            $sql = 'INSERT INTO reservations (' . implode(', ', $columns) . ') VALUES ('
                . implode(', ', array_fill(0, count($columns), '?')) . ')';
            try {
                $this->pdo->prepare($sql)->execute(array_values($fields));
                break;
            } catch (\PDOException $error) {
                $duplicateCode = (int) ($error->errorInfo[1] ?? 0) === 1062
                    && str_contains($error->getMessage(), 'uq_reservations_code');
                if ((int) ($error->errorInfo[1] ?? 0) === 1062 && str_contains($error->getMessage(), 'uq_reservations_request_id')) {
                    throw new DuplicateRequest();
                }
                if (!$duplicateCode || $attempt >= 5) {
                    throw $error;
                }
            }
        }
        $id = (int) $this->pdo->lastInsertId();
        $this->replaceStations($id, $stationIds);

        return $id;
    }

    public function idForRequest(string $requestId): ?int
    {
        $statement = $this->pdo->prepare('SELECT id FROM reservations WHERE request_id = ?');
        $statement->execute([$requestId]);
        $id = $statement->fetchColumn();

        return $id === false ? null : (int) $id;
    }

    public function find(int $id, \DateTimeZone $tz, bool $forUpdate = false): ?Reservation
    {
        $statement = $this->pdo->prepare('SELECT * FROM reservations WHERE id = ?' . ($forUpdate ? ' FOR UPDATE' : ''));
        $statement->execute([$id]);
        $row = $statement->fetch();
        if ($row === false) {
            return null;
        }
        $stations = $this->pdo->prepare('SELECT station_id FROM reservation_stations WHERE reservation_id = ? ORDER BY station_id');
        $stations->execute([$id]);
        [$startMinute] = self::localMinutes((string) $row['start_utc'], (string) $row['end_utc'], $tz);

        return new Reservation(
            (int) $row['id'],
            (string) $row['uuid'],
            (string) $row['confirmation_code'],
            (string) $row['status'],
            (string) $row['local_date'],
            $startMinute,
            (int) $row['duration_minutes'],
            array_map('intval', $stations->fetchAll(PDO::FETCH_COLUMN)),
            (string) $row['start_utc'],
            (string) $row['end_utc'],
            (int) $row['subtotal_cents'],
            (int) $row['tax_cents'],
            (int) $row['total_cents'],
            (string) $row['currency'],
            $row['hold_expires_at'] === null ? null : (string) $row['hold_expires_at'],
            $row['payment_id'] === null ? null : (string) $row['payment_id'],
        );
    }

    /**
     * Every column of one reservation plus local minutes and station numbers. For staff and email only.
     *
     * @return array<string,mixed>|null
     */
    public function findRow(int $id, \DateTimeZone $tz): ?array
    {
        $rows = $this->rows('r.id = ?', [$id], $tz);

        return $rows[0] ?? null;
    }

    /**
     * Every reservation on a local date, all statuses, ordered by start time. For staff only.
     *
     * @return array<int, array<string,mixed>>
     */
    public function listForDate(string $localDate, \DateTimeZone $tz): array
    {
        return $this->rows('r.local_date = ?', [$localDate], $tz);
    }

    public function updateContact(int $id, string $firstName, string $lastName, string $email, string $phone, ?string $comments, \DateTimeImmutable $nowUtc): void
    {
        $this->pdo->prepare(
            'UPDATE reservations SET first_name = ?, last_name = ?, email = ?, phone = ?, comments = ?, updated_at = ? WHERE id = ?'
        )->execute([$firstName, $lastName, $email, $phone, $comments, $nowUtc->format('Y-m-d H:i:s'), $id]);
    }

    /** @param int[] $stationIds */
    public function updateSchedule(int $id, string $localDate, string $startUtc, string $endUtc, int $durationMinutes, int $stationCount, Quote $quote, array $stationIds, \DateTimeImmutable $nowUtc): void
    {
        $this->pdo->prepare(
            'UPDATE reservations SET local_date = ?, start_utc = ?, end_utc = ?, duration_minutes = ?, station_count = ?, '
            . 'subtotal_cents = ?, tax_cents = ?, total_cents = ?, currency = ?, updated_at = ? WHERE id = ?'
        )->execute([
            $localDate, $startUtc, $endUtc, $durationMinutes, $stationCount,
            $quote->subtotalCents, $quote->taxCents, $quote->totalCents, $quote->currency,
            $nowUtc->format('Y-m-d H:i:s'), $id,
        ]);
        $this->replaceStations($id, $stationIds);
    }

    /**
     * @param string[] $fromStatuses
     * @return bool true when a row changed
     */
    public function transition(int $id, array $fromStatuses, string $toStatus, \DateTimeImmutable $nowUtc, ?string $provider = null, ?string $paymentId = null): bool
    {
        $placeholders = implode(', ', array_fill(0, count($fromStatuses), '?'));
        $sql = 'UPDATE reservations SET status = ?, updated_at = ?, hold_expires_at = NULL';
        $params = [$toStatus, $nowUtc->format('Y-m-d H:i:s')];
        if ($provider !== null) {
            $sql .= ', payment_provider = ?, payment_id = ?';
            $params[] = $provider;
            $params[] = $paymentId;
        }
        $sql .= " WHERE id = ? AND status IN ({$placeholders})";
        $statement = $this->pdo->prepare($sql);
        $statement->execute([...$params, $id, ...$fromStatuses]);

        // rowCount() counts rows changed by the UPDATE, which is reliable here because every
        // transition changes `status`: a row matching the WHERE clause can never already equal
        // $toStatus, since $toStatus is never one of the $fromStatuses it was matched against.
        return $statement->rowCount() === 1;
    }

    public function expireHolds(\DateTimeImmutable $nowUtc): int
    {
        $statement = $this->pdo->prepare(
            "UPDATE reservations SET status = 'expired', updated_at = ?, hold_expires_at = NULL "
            . "WHERE status = 'held' AND hold_expires_at <= ?"
        );
        $now = $nowUtc->format('Y-m-d H:i:s');
        $statement->execute([$now, $now]);

        return $statement->rowCount();
    }

    /**
     * Held reservations whose hold has passed, with what reconciliation needs to look their payment up.
     *
     * @return array<int, array{id:int,code:string,createdAt:\DateTimeImmutable,holdExpiresAt:\DateTimeImmutable}>
     */
    public function overdueHolds(\DateTimeImmutable $nowUtc): array
    {
        $statement = $this->pdo->prepare(
            "SELECT id, confirmation_code, created_at, hold_expires_at FROM reservations WHERE status = 'held' AND hold_expires_at <= ? ORDER BY id"
        );
        $statement->execute([$nowUtc->format('Y-m-d H:i:s')]);
        $utc = new \DateTimeZone('UTC');
        $result = [];
        foreach ($statement->fetchAll() as $row) {
            $result[] = [
                'id' => (int) $row['id'],
                'code' => (string) $row['confirmation_code'],
                'createdAt' => new \DateTimeImmutable((string) $row['created_at'], $utc),
                'holdExpiresAt' => new \DateTimeImmutable((string) $row['hold_expires_at'], $utc),
            ];
        }

        return $result;
    }

    /** Anonymises personal details of reservations whose date is older than the cut-off. Returns rows changed. */
    public function anonymiseBefore(string $localDate, \DateTimeImmutable $nowUtc): int
    {
        $statement = $this->pdo->prepare(
            "UPDATE reservations SET first_name = 'Removed', last_name = '', email = '', phone = '', comments = NULL, updated_at = ? "
            . "WHERE local_date < ? AND email <> '' AND status IN ('confirmed', 'cancelled', 'expired', 'payment_failed')"
        );
        $statement->execute([$nowUtc->format('Y-m-d H:i:s'), $localDate]);

        return $statement->rowCount();
    }

    /** @param int[] $stationIds */
    private function replaceStations(int $id, array $stationIds): void
    {
        $this->pdo->prepare('DELETE FROM reservation_stations WHERE reservation_id = ?')->execute([$id]);
        $link = $this->pdo->prepare('INSERT INTO reservation_stations (reservation_id, station_id) VALUES (?, ?)');
        foreach ($stationIds as $stationId) {
            $link->execute([$id, $stationId]);
        }
    }

    /**
     * @param array<int|string> $params
     * @return array<int, array<string,mixed>>
     */
    private function rows(string $where, array $params, \DateTimeZone $tz): array
    {
        $statement = $this->pdo->prepare(
            'SELECT r.*, GROUP_CONCAT(s.number ORDER BY s.number) AS station_numbers '
            . 'FROM reservations r '
            . 'LEFT JOIN reservation_stations rs ON rs.reservation_id = r.id '
            . 'LEFT JOIN stations s ON s.id = rs.station_id '
            . "WHERE {$where} GROUP BY r.id ORDER BY r.start_utc, r.id"
        );
        $statement->execute($params);
        $result = [];
        foreach ($statement->fetchAll() as $row) {
            [$startMinute, $endMinute] = self::localMinutes((string) $row['start_utc'], (string) $row['end_utc'], $tz);
            $numbers = $row['station_numbers'] === null || $row['station_numbers'] === ''
                ? []
                : array_map('intval', explode(',', (string) $row['station_numbers']));
            $result[] = [
                'id' => (int) $row['id'],
                'uuid' => (string) $row['uuid'],
                'confirmation_code' => (string) $row['confirmation_code'],
                'status' => (string) $row['status'],
                'first_name' => (string) $row['first_name'],
                'last_name' => (string) $row['last_name'],
                'email' => (string) $row['email'],
                'phone' => (string) $row['phone'],
                'comments' => $row['comments'] === null ? null : (string) $row['comments'],
                'date' => (string) $row['local_date'],
                'start_minute' => $startMinute,
                'end_minute' => $endMinute,
                'duration_minutes' => (int) $row['duration_minutes'],
                'station_count' => (int) $row['station_count'],
                'stations' => $numbers,
                'subtotal_cents' => (int) $row['subtotal_cents'],
                'tax_cents' => (int) $row['tax_cents'],
                'total_cents' => (int) $row['total_cents'],
                'currency' => (string) $row['currency'],
                'payment_provider' => (string) $row['payment_provider'],
                'payment_id' => $row['payment_id'] === null ? null : (string) $row['payment_id'],
                'hold_expires_at' => $row['hold_expires_at'] === null ? null : (string) $row['hold_expires_at'],
                'created_by' => (string) $row['created_by'],
                'created_at' => (string) $row['created_at'],
                'updated_at' => (string) $row['updated_at'],
            ];
        }

        return $result;
    }

    /** @return array{0:int,1:int} start and end as minutes after local midnight of the start date */
    private static function localMinutes(string $startUtc, string $endUtc, \DateTimeZone $tz): array
    {
        $utc = new \DateTimeZone('UTC');
        $start = (new \DateTimeImmutable($startUtc, $utc))->setTimezone($tz);
        $end = (new \DateTimeImmutable($endUtc, $utc))->setTimezone($tz);
        $startMinute = (int) $start->format('G') * 60 + (int) $start->format('i');
        $endMinute = $end->format('Y-m-d') > $start->format('Y-m-d')
            ? 1440
            : (int) $end->format('G') * 60 + (int) $end->format('i');

        return [$startMinute, $endMinute];
    }

    private static function uuid(): string
    {
        $bytes = random_bytes(16);
        $bytes[6] = chr((ord($bytes[6]) & 0x0f) | 0x40);
        $bytes[8] = chr((ord($bytes[8]) & 0x3f) | 0x80);

        return vsprintf('%s%s-%s-%s-%s-%s%s%s', str_split(bin2hex($bytes), 4));
    }

    private static function confirmationCode(): string
    {
        $code = '';
        $max = strlen(self::CODE_ALPHABET) - 1;
        for ($i = 0; $i < 8; $i++) {
            $code .= self::CODE_ALPHABET[random_int(0, $max)];
        }

        return $code;
    }
}
