<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Console\Doctor;
use ArcadeOS\Console\Installer;
use ArcadeOS\Support\Config;
use ArcadeOS\Support\Env;
use ArcadeOS\Support\FixedClock;
use ArcadeOS\Tests\Support\FakeHttpClient;
use PDO;
use PHPUnit\Framework\TestCase;

final class DoctorAndInstallerTest extends TestCase
{
    private PDO $pdo;

    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
        $this->pdo = TestDb::connect();
        $this->pdo->exec('SET FOREIGN_KEY_CHECKS = 0');
        $query = $this->pdo->query('SHOW TABLES');
        foreach ($query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN) as $table) {
            $this->pdo->exec('DROP TABLE `' . str_replace('`', '', (string) $table) . '`');
        }
        $this->pdo->exec('SET FOREIGN_KEY_CHECKS = 1');
    }

    /** @param array<string,string> $env */
    private function doctor(array $env, ?PDO $pdo, FakeHttpClient $http, bool $online = false): Doctor
    {
        $config = new Config(new Env($env + ['APP_KEY' => 'test-key-0123456789abcdef0123456789abcdef', 'APP_ENV' => 'production'], false), dirname(__DIR__, 2));

        return new Doctor($config, $pdo, $pdo === null ? 'connection refused' : null, dirname(__DIR__, 2) . '/migrations', $http, $online);
    }

    /** @param array<int, array{check:string,status:string,detail:string}> $results */
    private function statusOf(array $results, string $check): string
    {
        foreach ($results as $result) {
            if ($result['check'] === $check) {
                return $result['status'];
            }
        }

        return 'missing';
    }

    public function testFreshDatabaseFailsUntilInstalled(): void
    {
        $doctor = $this->doctor(['APP_URL' => 'https://booking.example.com'], $this->pdo, new FakeHttpClient());
        $before = $doctor->run();
        self::assertSame('fail', $this->statusOf($before, 'db.migrations'));
        self::assertTrue(Doctor::hasFailures($before));
        self::assertSame('warn', $this->statusOf($before, 'payment.mode'));
        self::assertSame('skip', $this->statusOf($before, 'web.exposure'));

        $lines = (new Installer($this->pdo, dirname(__DIR__, 2) . '/migrations', new FixedClock('2026-09-21 14:00:00')))
            ->install('owner', 'correct-horse-battery', ['venue' => 'Orbit VR', 'stations' => 3]);
        self::assertStringContainsString('Admin owner created.', implode("\n", $lines));

        $after = $doctor->run();
        foreach (['db.connection', 'db.migrations', 'db.admin', 'db.stations', 'db.prices', 'db.hours', 'env.app_key', 'env.debug'] as $check) {
            self::assertSame('pass', $this->statusOf($after, $check), $check);
        }
        self::assertFalse(Doctor::hasFailures($after));
    }

    public function testConfigurationProblemsAreNamed(): void
    {
        $results = $this->doctor([
            'APP_KEY' => 'short', 'APP_DEBUG' => 'true', 'APP_URL' => 'http://booking.example.com',
            'PAYMENT_MODE' => 'square', 'SETUP_TOKEN' => 'leftover-token', 'MAIL_DRIVER' => 'smtp',
        ], null, new FakeHttpClient())->run();
        self::assertSame('fail', $this->statusOf($results, 'env.app_key'));
        self::assertSame('fail', $this->statusOf($results, 'env.debug'));
        self::assertSame('warn', $this->statusOf($results, 'env.app_url'));
        self::assertSame('warn', $this->statusOf($results, 'env.setup_token'));
        self::assertSame('fail', $this->statusOf($results, 'db.connection'));
        self::assertSame('fail', $this->statusOf($results, 'payment.square'), 'square selected without credentials');
        self::assertSame('fail', $this->statusOf($results, 'mail.from'));
    }

    public function testOnlineChecksUseTheHttpClient(): void
    {
        (new Installer($this->pdo, dirname(__DIR__, 2) . '/migrations', new FixedClock('2026-09-21 14:00:00')))->install('owner', 'correct-horse-battery');
        $http = new FakeHttpClient();
        // Square location lookup, then five private paths, then the API, then the booking page.
        $http->queue(200, ['location' => ['id' => 'LOC123']]);
        $http->queue(200, 'APP_KEY=leaked')->queue(403, '')->queue(404, '')->queue(404, '')->queue(404, '');
        $http->queue(200, '{"venue":{}}')->queue(200, '<html>');
        $results = $this->doctor([
            'APP_URL' => 'https://booking.example.com', 'PAYMENT_MODE' => 'square', 'SQUARE_ENV' => 'production',
            'SQUARE_ACCESS_TOKEN' => 'test-token', 'SQUARE_APPLICATION_ID' => 'app', 'SQUARE_LOCATION_ID' => 'LOC123',
        ], $this->pdo, $http, true)->run();
        self::assertSame('pass', $this->statusOf($results, 'payment.square.credentials'));
        self::assertSame('https://connect.squareup.com/v2/locations/LOC123', $http->requests[0]['url']);
        self::assertSame('fail', $this->statusOf($results, 'web.private..env'), 'a readable .env must fail');
        self::assertSame('pass', $this->statusOf($results, 'web.private.src.Http.App.php'));
        self::assertSame('pass', $this->statusOf($results, 'web.api'));
        self::assertSame('pass', $this->statusOf($results, 'web.booking_page'));
        self::assertTrue(Doctor::hasFailures($results));
    }

    public function testInstallerRefusesWeakCredentialsBeforeTouchingTheDatabase(): void
    {
        $installer = new Installer($this->pdo, dirname(__DIR__, 2) . '/migrations', new FixedClock('2026-09-21 14:00:00'));
        try {
            $installer->install('owner', 'short');
            self::fail('expected InvalidArgumentException');
        } catch (\InvalidArgumentException) {
            self::assertTrue(true);
        }
        $query = $this->pdo->query('SHOW TABLES');
        self::assertSame([], $query === false ? ['?'] : $query->fetchAll(PDO::FETCH_COLUMN), 'nothing was created');
        self::assertFalse($installer->hasAdmin());
    }
}
