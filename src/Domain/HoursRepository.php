<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

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

    /** @return array<int, array{weekday:int,open_minute:int,close_minute:int,closed:bool}> keyed 0..6; missing weekdays are closed */
    public function weekdays(): array
    {
        $result = [];
        for ($weekday = 0; $weekday <= 6; $weekday++) {
            $result[$weekday] = ['weekday' => $weekday, 'open_minute' => 600, 'close_minute' => 1320, 'closed' => true];
        }
        $query = $this->pdo->query('SELECT weekday, open_minute, close_minute, closed FROM business_hours');
        foreach ($query === false ? [] : $query->fetchAll() as $row) {
            $result[(int) $row['weekday']] = [
                'weekday' => (int) $row['weekday'],
                'open_minute' => (int) $row['open_minute'],
                'close_minute' => (int) $row['close_minute'],
                'closed' => (int) $row['closed'] === 1,
            ];
        }

        return $result;
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

    /** @return array<int, array{date:string,open_minute:int,close_minute:int}> from today onwards, ascending */
    public function specialHoursFrom(string $localDate): array
    {
        self::assertDate($localDate);
        $statement = $this->pdo->prepare('SELECT local_date, open_minute, close_minute FROM special_hours WHERE local_date >= ? ORDER BY local_date');
        $statement->execute([$localDate]);
        $result = [];
        foreach ($statement->fetchAll() as $row) {
            $result[] = ['date' => (string) $row['local_date'], 'open_minute' => (int) $row['open_minute'], 'close_minute' => (int) $row['close_minute']];
        }

        return $result;
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

    public function removeSpecialHours(string $localDate): void
    {
        self::assertDate($localDate);
        $this->pdo->prepare('DELETE FROM special_hours WHERE local_date = ?')->execute([$localDate]);
    }

    /** @return array<int, array{date:string,reason:string}> from the given date onwards, ascending */
    public function closedDatesFrom(string $localDate): array
    {
        self::assertDate($localDate);
        $statement = $this->pdo->prepare('SELECT local_date, reason FROM closed_dates WHERE local_date >= ? ORDER BY local_date');
        $statement->execute([$localDate]);
        $result = [];
        foreach ($statement->fetchAll() as $row) {
            $result[] = ['date' => (string) $row['local_date'], 'reason' => (string) $row['reason']];
        }

        return $result;
    }

    public function addClosedDate(string $localDate, string $reason): void
    {
        self::assertDate($localDate);
        $this->pdo->prepare(
            'INSERT INTO closed_dates (local_date, reason) VALUES (?, ?) ON DUPLICATE KEY UPDATE reason = VALUES(reason)'
        )->execute([$localDate, mb_substr($reason, 0, 120)]);
    }

    public function removeClosedDate(string $localDate): void
    {
        self::assertDate($localDate);
        $this->pdo->prepare('DELETE FROM closed_dates WHERE local_date = ?')->execute([$localDate]);
    }
}
