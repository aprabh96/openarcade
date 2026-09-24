<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

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

    /** @return array<int, array{number:int,label:string}> active stations ordered by number */
    public function active(): array
    {
        $query = $this->pdo->query('SELECT number, label FROM stations WHERE active = 1 ORDER BY number');
        $result = [];
        foreach ($query === false ? [] : $query->fetchAll() as $row) {
            $result[] = ['number' => (int) $row['number'], 'label' => (string) $row['label']];
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

    public function setLabel(int $number, string $label): void
    {
        $label = trim($label);
        if ($label === '' || mb_strlen($label) > 60) {
            throw new \InvalidArgumentException('Station label must be 1 to 60 characters.');
        }
        $this->pdo->prepare('UPDATE stations SET label = ? WHERE number = ?')->execute([$label, $number]);
    }
}
