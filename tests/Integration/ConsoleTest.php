<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Console\Application;
use ArcadeOS\Support\FixedClock;
use PDO;
use PHPUnit\Framework\TestCase;

final class ConsoleTest extends TestCase
{
    private PDO $pdo;
    /** @var string[] */
    private array $output = [];

    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
        $this->pdo = TestDb::fresh();
        $this->output = [];
    }

    /** @param string[] $args */
    private function console(array $args): int
    {
        $app = new Application(
            $this->pdo,
            dirname(__DIR__, 2) . '/migrations',
            new FixedClock('2026-09-21 14:00:00'),
            function (string $line): void {
                $this->output[] = $line;
            }
        );

        return $app->run(['console', ...$args]);
    }

    private function rows(string $table): int
    {
        $query = $this->pdo->query("SELECT COUNT(*) FROM {$table}");

        return $query === false ? -1 : (int) $query->fetchColumn();
    }

    public function testInstallSeedsDefaultsAndCreatesAdminOnce(): void
    {
        $args = ['install', '--admin-user=owner', '--admin-password=correct-horse-battery', '--stations=5', '--venue=Orbit VR', '--timezone=America/Denver'];
        self::assertSame(0, $this->console($args));
        self::assertSame(5, $this->rows('stations'));
        self::assertSame(7, $this->rows('business_hours'));
        self::assertSame(3, $this->rows('prices'));
        self::assertSame(1, $this->rows('admins'));
        $hash = $this->pdo->query("SELECT password_hash FROM admins WHERE username = 'owner'");
        self::assertTrue(password_verify('correct-horse-battery', $hash === false ? '' : (string) $hash->fetchColumn()));

        self::assertSame(0, $this->console($args), 'install is safe to run again');
        self::assertSame(1, $this->rows('admins'));
        self::assertSame(3, $this->rows('prices'));
    }

    public function testShortPasswordIsRefusedAndChangesNothing(): void
    {
        self::assertSame(1, $this->console(['install', '--admin-user=owner', '--admin-password=short']));
        self::assertSame(0, $this->rows('admins'));
        self::assertSame(0, $this->rows('stations'));
        self::assertStringContainsString('12 characters', implode("\n", $this->output));
    }

    public function testSeedDemoCreatesFakeReservationsOnly(): void
    {
        $this->console(['install', '--admin-user=owner', '--admin-password=correct-horse-battery']);
        self::assertSame(0, $this->console(['seed:demo']));
        self::assertGreaterThanOrEqual(8, $this->rows('reservations'));
        $real = $this->pdo->query("SELECT COUNT(*) FROM reservations WHERE email NOT LIKE '%@example.com'");
        self::assertSame(0, $real === false ? -1 : (int) $real->fetchColumn());
    }

    public function testUnknownCommandShowsHelpAndFails(): void
    {
        self::assertSame(1, $this->console(['nonsense']));
        self::assertStringContainsString('install', implode("\n", $this->output));
    }
}
