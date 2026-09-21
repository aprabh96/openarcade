<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Db\Migrator;
use PDO;
use PHPUnit\Framework\TestCase;

final class MigrationTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testMigrationsCreateEveryTableAndAreIdempotent(): void
    {
        $pdo = TestDb::fresh();
        $query = $pdo->query('SHOW TABLES');
        $tables = $query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN);
        foreach (['settings', 'stations', 'business_hours', 'special_hours', 'closed_dates', 'prices', 'booking_days', 'reservations', 'reservation_stations', 'admins', 'login_attempts', 'rate_limits', 'migrations'] as $expected) {
            self::assertContains($expected, $tables);
        }
        $again = (new Migrator($pdo, dirname(__DIR__, 2) . '/migrations'))->migrate();
        self::assertSame([], $again);
    }

    public function testConnectionUsesUtc(): void
    {
        $pdo = TestDb::connect();
        $query = $pdo->query('SELECT @@session.time_zone');
        self::assertSame('+00:00', $query === false ? null : $query->fetchColumn());
    }
}
