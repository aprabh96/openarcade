<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Db\Connection;
use ArcadeOS\Db\Migrator;
use PDO;

final class TestDb
{
    public static function available(): bool
    {
        return getenv('DB_HOST') !== false && getenv('TEST_DB_NAME') !== false;
    }

    public static function connect(): PDO
    {
        return Connection::make(
            (string) getenv('DB_HOST'),
            (int) (getenv('DB_PORT') ?: 3306),
            (string) getenv('TEST_DB_NAME'),
            (string) getenv('DB_USER'),
            (string) getenv('DB_PASSWORD'),
        );
    }

    /** Drops every table and re-applies all migrations. */
    public static function fresh(): PDO
    {
        $pdo = self::connect();
        $pdo->exec('SET FOREIGN_KEY_CHECKS = 0');
        $query = $pdo->query('SHOW TABLES');
        foreach ($query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN) as $table) {
            $pdo->exec('DROP TABLE `' . str_replace('`', '', (string) $table) . '`');
        }
        $pdo->exec('SET FOREIGN_KEY_CHECKS = 1');
        (new Migrator($pdo, dirname(__DIR__, 2) . '/migrations'))->migrate();

        return $pdo;
    }
}
