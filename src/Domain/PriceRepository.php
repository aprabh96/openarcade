<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

use PDO;

final class PriceRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    public function load(): PriceList
    {
        $byWeekday = [];
        foreach ($this->all() as $row) {
            $byWeekday[$row['weekday']][$row['duration_minutes']] = $row['price_cents'];
        }

        return new PriceList($byWeekday);
    }

    /** @return array<int, array{weekday:int,duration_minutes:int,price_cents:int}> ordered by weekday then duration */
    public function all(): array
    {
        $query = $this->pdo->query('SELECT weekday, duration_minutes, price_cents FROM prices ORDER BY weekday, duration_minutes');
        $result = [];
        foreach ($query === false ? [] : $query->fetchAll() as $row) {
            $result[] = [
                'weekday' => (int) $row['weekday'],
                'duration_minutes' => (int) $row['duration_minutes'],
                'price_cents' => (int) $row['price_cents'],
            ];
        }

        return $result;
    }

    public function set(int $weekday, int $durationMinutes, int $priceCents): void
    {
        $this->assertValid($weekday, $durationMinutes);
        if ($priceCents < 0) {
            throw new \InvalidArgumentException('Price cannot be negative.');
        }
        $this->pdo->prepare(
            'INSERT INTO prices (weekday, duration_minutes, price_cents) VALUES (?, ?, ?) '
            . 'ON DUPLICATE KEY UPDATE price_cents = VALUES(price_cents)'
        )->execute([$weekday, $durationMinutes, $priceCents]);
    }

    public function remove(int $weekday, int $durationMinutes): void
    {
        $this->assertValid($weekday, $durationMinutes);
        $this->pdo->prepare('DELETE FROM prices WHERE weekday = ? AND duration_minutes = ?')
            ->execute([$weekday, $durationMinutes]);
    }

    private function assertValid(int $weekday, int $durationMinutes): void
    {
        if ($weekday < PriceList::EVERY_DAY || $weekday > 6) {
            throw new \InvalidArgumentException('Weekday must be -1 (every day) or 0 (Sunday) to 6 (Saturday).');
        }
        if ($durationMinutes < 5 || $durationMinutes > 1440) {
            throw new \InvalidArgumentException('Duration must be from 5 to 1440 minutes.');
        }
    }
}
