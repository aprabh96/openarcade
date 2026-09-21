<?php

declare(strict_types=1);

namespace ArcadeOS\Db;

use PDO;

final class Migrator
{
    public function __construct(private PDO $pdo, private string $directory)
    {
    }

    /** @return string[] versions applied by this call */
    public function migrate(): array
    {
        $this->pdo->exec(
            'CREATE TABLE IF NOT EXISTS migrations ('
            . 'version VARCHAR(100) NOT NULL PRIMARY KEY, applied_at DATETIME NOT NULL'
            . ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
        );
        $query = $this->pdo->query('SELECT version FROM migrations');
        $done = $query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN);

        $files = glob($this->directory . '/*.sql') ?: [];
        sort($files);
        $applied = [];
        foreach ($files as $file) {
            $version = basename($file, '.sql');
            if (in_array($version, $done, true)) {
                continue;
            }
            foreach (self::statements((string) file_get_contents($file)) as $statement) {
                $this->pdo->exec($statement);
            }
            $this->pdo->prepare('INSERT INTO migrations (version, applied_at) VALUES (?, UTC_TIMESTAMP())')
                ->execute([$version]);
            $applied[] = $version;
        }

        return $applied;
    }

    /**
     * Migration files contain plain statements ending in ";" at end of line, and "--" comment lines.
     * The splitter does not support stored procedures, triggers, DELIMITER blocks, or string
     * literals that end a line with ";". Keep migrations to plain statements.
     *
     * @return string[]
     */
    public static function statements(string $sql): array
    {
        $sql = str_replace("\r\n", "\n", $sql);
        $sql = preg_replace('/^\s*--.*$/m', '', $sql) ?? $sql;
        $parts = array_map('trim', explode(";\n", $sql . "\n"));

        return array_values(array_filter($parts, static fn (string $part): bool => $part !== ''));
    }
}
