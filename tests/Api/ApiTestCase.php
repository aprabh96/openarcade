<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Api;

use ArcadeOS\Http\App;
use ArcadeOS\Http\ArraySession;
use ArcadeOS\Http\Request;
use ArcadeOS\Http\Response;
use ArcadeOS\Http\Services;
use ArcadeOS\Mail\LogMailer;
use ArcadeOS\Payments\NullGateway;
use ArcadeOS\Payments\PaymentGateway;
use ArcadeOS\Realtime\NullNotifier;
use ArcadeOS\Support\Config;
use ArcadeOS\Support\Env;
use ArcadeOS\Support\FixedClock;
use ArcadeOS\Support\Logger;
use ArcadeOS\Tests\Integration\TestDb;
use ArcadeOS\Tests\Integration\VenueFixture;
use PDO;
use PHPUnit\Framework\TestCase;

/**
 * Drives the whole HTTP layer in-process: real database, real routing, real session object,
 * no web server. Every test starts with the VenueFixture (2 stations, Chicago, 9.35% tax).
 */
abstract class ApiTestCase extends TestCase
{
    public const ADMIN_PASSWORD = 'correct-horse-battery';

    protected PDO $pdo;
    protected FixedClock $clock;
    protected ArraySession $session;
    protected NullNotifier $notifier;
    protected LogMailer $mailer;
    protected Logger $logger;
    protected Services $services;
    protected App $app;

    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
        $this->pdo = TestDb::fresh();
        VenueFixture::seed($this->pdo, 2);
        $this->clock = new FixedClock('2026-09-21 14:00:00');
        $this->session = new ArraySession();
        $this->boot(new NullGateway());
    }

    protected function boot(PaymentGateway $gateway): void
    {
        $config = new Config(new Env([
            'APP_ENV' => 'testing',
            'APP_DEBUG' => 'false',
            'APP_URL' => 'http://localhost',
            'APP_KEY' => 'test-key-0123456789abcdef0123456789abcdef',
            'EMBED_ALLOWED_ORIGINS' => 'https://venue.example.com',
            'PAYMENT_MODE' => $gateway->mode(),
            'SQUARE_ENV' => 'sandbox',
            'SQUARE_ACCESS_TOKEN' => 'test-access-token',
            'SQUARE_APPLICATION_ID' => 'sandbox-app-id',
            'SQUARE_LOCATION_ID' => 'LOC123',
        ], false), dirname(__DIR__, 2));
        $this->notifier = new NullNotifier();
        $this->mailer = new LogMailer();
        $this->logger = new Logger(null);
        $this->services = Services::build($config, $this->pdo, $this->clock, $gateway, $this->notifier, $this->mailer, $this->logger);
        $this->app = new App($this->services);
    }

    /**
     * @param array<string,mixed> $body
     * @param array<string,string> $headers
     */
    protected function request(string $method, string $path, array $body = [], array $headers = [], string $ip = '203.0.113.5', bool $malformed = false): Response
    {
        $query = [];
        $parts = parse_url($path);
        if (is_array($parts) && isset($parts['query'])) {
            parse_str($parts['query'], $parsed);
            foreach ($parsed as $key => $value) {
                if (is_string($value)) {
                    $query[(string) $key] = $value;
                }
            }
        }
        $cleanPath = is_array($parts) && isset($parts['path']) ? $parts['path'] : $path;
        $normalized = [];
        foreach ($headers as $name => $value) {
            $normalized[strtolower($name)] = $value;
        }
        if ($method !== 'GET' && !isset($normalized['origin']) && !array_key_exists('no-origin', $normalized)) {
            $normalized['origin'] = 'http://localhost';
        }
        unset($normalized['no-origin']);
        $normalized['host'] ??= 'localhost';

        return $this->app->handle(new Request($method, $cleanPath, $query, $body, $normalized, $ip, $this->session, false, $malformed));
    }

    /** @return array<string,mixed> */
    protected function json(Response $response): array
    {
        $decoded = $response->decode();
        self::assertIsArray($decoded, 'body: ' . $response->body);

        return $decoded;
    }

    protected function bookingToken(): string
    {
        $data = $this->json($this->request('GET', '/api/booking-token'));

        return (string) $data['token'];
    }

    protected function createAdmin(string $username = 'owner', string $password = self::ADMIN_PASSWORD): void
    {
        $this->pdo->prepare('INSERT IGNORE INTO admins (username, password_hash, created_at) VALUES (?, ?, ?)')
            ->execute([$username, password_hash($password, PASSWORD_DEFAULT), '2026-09-01 00:00:00']);
    }

    /** Creates the admin, signs in, returns the CSRF token. */
    protected function signIn(): string
    {
        $this->createAdmin();
        $response = $this->request('POST', '/api/admin/login', ['username' => 'owner', 'password' => self::ADMIN_PASSWORD]);
        self::assertSame(200, $response->status, $response->body);

        return (string) $this->json($response)['csrf_token'];
    }

    /** @param array<string,mixed> $body */
    protected function adminRequest(string $csrf, string $method, string $path, array $body = []): Response
    {
        return $this->request($method, $path, $body, ['X-CSRF-Token' => $csrf]);
    }

    /** @return array<string,mixed> */
    protected function customerBooking(string $date = '2026-09-22', int $start = 600, int $stations = 1, string $email = 'alex.rivera@example.com'): array
    {
        return [
            'booking_token' => $this->bookingToken(),
            'date' => $date,
            'start_minute' => $start,
            'duration_minutes' => 60,
            'station_count' => $stations,
            'first_name' => 'Alex',
            'last_name' => 'Rivera',
            'email' => $email,
            'phone' => '785-555-0142',
        ];
    }
}
