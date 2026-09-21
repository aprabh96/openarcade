<?php

declare(strict_types=1);

namespace ArcadeOS\Db;

use ArcadeOS\Support\Config;
use PDO;

final class Connection
{
    public static function make(string $host, int $port, string $database, string $user, string $password): PDO
    {
        $pdo = new PDO(
            "mysql:host={$host};port={$port};dbname={$database};charset=utf8mb4",
            $user,
            $password,
            [
                PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                PDO::ATTR_EMULATE_PREPARES => false,
            ]
        );
        // Every DATETIME in this application is UTC.
        $pdo->exec("SET time_zone = '+00:00'");

        return $pdo;
    }

    public static function fromConfig(Config $config): PDO
    {
        $db = $config->database();

        return self::make($db['host'], $db['port'], $db['name'], $db['user'], $db['password']);
    }
}
