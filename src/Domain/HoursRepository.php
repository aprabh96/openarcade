<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

use PDO;

final class HoursRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    public static function assertDate(string $localDate): void
    {
        $parsed = \DateTimeImmutable::createFromFormat('!Y-m-d', $localDate);
        if ($parsed === false || $parsed->format('Y-m-d') !== $localDate) {
            throw new \InvalidArgumentException('Date must be YYYY-MM-DD.');
        }
    }

    /** Hours for a local date, or null when the venue is closed that day. */
    public function forDate(string $localDate): ?DayHours
    {
        self::assertDate($localDate);

        $closed = $this->pdo->prepare('SELECT 1 FROM closed_dates WHERE local_date = ?');
        $closed->execute([$localDate]);
        if ($closed->fetchColumn() !== false) {
            return null;
        }

        $special = $this->pdo->prepare('SELECT open_minute, close_minute FROM special_hours WHERE local_date = ?');
        $special->execute([$localDate]);
        $row = $special->fetch();
        if ($row !== false) {
            return new DayHours((int) $row['open_minute'], (int) $row['close_minute']);
        }

        $weekday = (int) (new \DateTimeImmutable($localDate))->format('w');
        $regular = $this->pdo->prepare('SELECT open_minute, close_minute, closed FROM business_hours WHERE weekday = ?');
        $regular->execute([$weekday]);
        $row = $regular->fetch();
        if ($row === false || (int) $row['closed'] === 1) {
            return null;
        }

        return new DayHours((int) $row['open_minute'], (int) $row['close_minute']);
    }

    public function setWeekday(int $weekday, int $openMinute, int $closeMinute, bool $closed): void
    {
        if ($weekday < 0 || $weekday > 6) {
            throw new \InvalidArgumentException('Weekday must be 0 (Sunday) to 6 (Saturday).');
        }
        new DayHours($openMinute, $closeMinute);
        $this->pdo->prepare(
            'INSERT INTO business_hours (weekday, open_minute, close_minute, closed) VALUES (?, ?, ?, ?) '
            . 'ON DUPLICATE KEY UPDATE open_minute = VALUES(open_minute), close_minute = VALUES(close_minute), closed = VALUES(closed)'
        )->execute([$weekday, $openMinute, $closeMinute, $closed ? 1 : 0]);
    }

    public function setSpecialHours(string $localDate, int $openMinute, int $closeMinute): void
    {
        self::assertDate($localDate);
        new DayHours($openMinute, $closeMinute);
        $this->pdo->prepare(
            'INSERT INTO special_hours (local_date, open_minute, close_minute) VALUES (?, ?, ?) '
            . 'ON DUPLICATE KEY UPDATE open_minute = VALUES(open_minute), close_minute = VALUES(close_minute)'
        )->execute([$localDate, $openMinute, $closeMinute]);
    }

    public function addClosedDate(string $localDate, string $reason): void
    {
        self::assertDate($localDate);
        $this->pdo->prepare(
            'INSERT INTO closed_dates (local_date, reason) VALUES (?, ?) ON DUPLICATE KEY UPDATE reason = VALUES(reason)'
        )->execute([$localDate, mb_substr($reason, 0, 120)]);
    }
}
