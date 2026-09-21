<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

use PDO;

final class StationRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    /** @return array<int,int> station id => station number, active stations only, ordered by number */
    public function activeNumbersById(): array
    {
        $query = $this->pdo->query('SELECT id, number FROM stations WHERE active = 1 ORDER BY number');
        $result = [];
        foreach ($query === false ? [] : $query->fetchAll() as $row) {
            $result[(int) $row['id']] = (int) $row['number'];
        }

        return $result;
    }

    /** Makes stations 1..$count active (creating them when needed) and deactivates the rest. */
    public function syncCount(int $count): void
    {
        if ($count < 1 || $count > 200) {
            throw new \InvalidArgumentException('Station count must be from 1 to 200.');
        }
        $insert = $this->pdo->prepare(
            'INSERT INTO stations (number, label, active) VALUES (?, ?, 1) ON DUPLICATE KEY UPDATE active = 1'
        );
        for ($number = 1; $number <= $count; $number++) {
            $insert->execute([$number, 'Station ' . $number]);
        }
        $this->pdo->prepare('UPDATE stations SET active = 0 WHERE number > ?')->execute([$count]);
    }
}
