<?php

declare(strict_types=1);

namespace ArcadeOS\Console;

use ArcadeOS\Domain\HoursRepository;
use ArcadeOS\Domain\PriceRepository;
use ArcadeOS\Domain\StationRepository;
use ArcadeOS\Support\Config;
use ArcadeOS\Support\HttpClient;
use PDO;

/**
 * Checks an installation the way a careful engineer would before go-live. Every check has a
 * stable name, a status (pass, warn, fail, skip) and a sentence saying what to do about it, so
 * an AI agent can loop on `doctor --json` until nothing fails.
 */
final class Doctor
{
    /** @var array<int, array{check:string,status:string,detail:string}> */
    private array $results = [];

    public function __construct(
        private Config $config,
        private ?PDO $pdo,
        private ?string $databaseError,
        private string $migrationsDir,
        private HttpClient $http,
        private bool $online,
    ) {
    }

    /** @return array<int, array{check:string,status:string,detail:string}> */
    public function run(): array
    {
        $this->results = [];
        $this->checkPhp();
        $this->checkEnvironment();
        $this->checkStorage();
        $this->checkDatabase();
        $this->checkPayment();
        $this->checkRealtime();
        $this->checkMail();
        $this->checkWeb();

        return $this->results;
    }

    /** @param array<int, array{check:string,status:string,detail:string}> $results */
    public static function hasFailures(array $results): bool
    {
        foreach ($results as $result) {
            if ($result['status'] === 'fail') {
                return true;
            }
        }

        return false;
    }

    private function checkPhp(): void
    {
        $this->add('php.version', PHP_VERSION_ID >= 80100 ? 'pass' : 'fail', 'PHP ' . PHP_VERSION . (PHP_VERSION_ID >= 80100 ? '' : '; PHP 8.1 or newer is required.'));
        foreach (['pdo_mysql', 'mbstring', 'curl', 'json', 'openssl'] as $extension) {
            $this->add('php.ext.' . $extension, extension_loaded($extension) ? 'pass' : 'fail', extension_loaded($extension) ? 'loaded' : "Enable the {$extension} extension in php.ini.");
        }
    }

    private function checkEnvironment(): void
    {
        $envFile = $this->config->rootDir() . '/.env';
        $this->add('env.file', is_file($envFile) ? 'pass' : 'warn', is_file($envFile) ? 'found' : 'No .env file; settings must come from real environment variables.');
        try {
            $key = $this->config->appKey();
            $placeholder = str_contains($key, 'not-for-production') || str_contains($key, 'change-me');
            $this->add('env.app_key', $placeholder ? 'fail' : 'pass', $placeholder ? 'APP_KEY is a published example value. Generate a new one with: php -r "echo bin2hex(random_bytes(32));"' : 'set');
        } catch (\RuntimeException $error) {
            $this->add('env.app_key', 'fail', $error->getMessage() . ' Generate one with: php -r "echo bin2hex(random_bytes(32));"');
        }
        $production = $this->config->appEnv() === 'production';
        $this->add('env.debug', $this->config->debug() && $production ? 'fail' : 'pass', $this->config->debug() ? 'APP_DEBUG is on' . ($production ? '; set APP_DEBUG=false in production.' : ' (fine outside production).') : 'off');
        $url = $this->config->appUrl();
        $https = str_starts_with($url, 'https://');
        $this->add('env.app_url', $https || !$production ? 'pass' : 'warn', $url . ($https ? '' : ' is not https; session cookies will not be marked Secure and card payments require https.'));
        try {
            $origins = $this->config->embedAllowedOrigins();
            $this->add('env.embed_origins', 'pass', $origins === [] ? 'none (the booking page cannot be embedded elsewhere)' : implode(', ', $origins));
        } catch (\RuntimeException $error) {
            $this->add('env.embed_origins', 'fail', $error->getMessage());
        }
        $token = $this->config->setupToken();
        $this->add('env.setup_token', $token === null || $token === '' ? 'pass' : ($this->pdo !== null && $this->count('admins') > 0 ? 'fail' : 'warn'), $token === null || $token === '' ? 'not set' : 'SETUP_TOKEN is still set; remove it from .env once setup is done.');
        $this->add('env.trust_proxy', $this->config->trustProxy() ? 'warn' : 'pass', $this->config->trustProxy() ? 'TRUST_PROXY is on; only correct behind a reverse proxy that overwrites X-Forwarded-For.' : 'off');
    }

    private function checkStorage(): void
    {
        foreach (['storage/logs', 'storage/cache'] as $dir) {
            $path = $this->config->rootDir() . '/' . $dir;
            $ok = is_dir($path) && is_writable($path);
            $this->add('storage.' . basename($dir), $ok ? 'pass' : 'fail', $ok ? 'writable' : "{$dir} must exist and be writable by the web server.");
        }
    }

    private function checkDatabase(): void
    {
        if ($this->pdo === null) {
            $this->add('db.connection', 'fail', 'Cannot connect: ' . ($this->databaseError ?? 'unknown error') . ' Check DB_HOST, DB_NAME, DB_USER and DB_PASSWORD.');

            return;
        }
        $this->add('db.connection', 'pass', 'connected');
        try {
            $pending = $this->pendingMigrations();
            $this->add('db.migrations', $pending === [] ? 'pass' : 'fail', $pending === [] ? 'up to date' : 'Pending: ' . implode(', ', $pending) . '. Run: php bin/console migrate');
            if ($pending !== []) {
                return;
            }
            $admins = $this->count('admins');
            $this->add('db.admin', $admins > 0 ? 'pass' : 'fail', $admins > 0 ? "{$admins} admin account(s)" : 'No admin account. Run: php bin/console install');
            $stations = count((new StationRepository($this->pdo))->activeNumbersById());
            $this->add('db.stations', $stations > 0 ? 'pass' : 'fail', $stations > 0 ? "{$stations} active station(s)" : 'No stations. Set the station count in the dashboard.');
            $prices = count((new PriceRepository($this->pdo))->all());
            $this->add('db.prices', $prices > 0 ? 'pass' : 'fail', $prices > 0 ? "{$prices} price row(s)" : 'No prices. Add at least one session length and price.');
            $open = 0;
            foreach ((new HoursRepository($this->pdo))->weekdays() as $day) {
                if (!$day['closed']) {
                    $open++;
                }
            }
            $this->add('db.hours', $open > 0 ? 'pass' : 'fail', $open > 0 ? "open {$open} day(s) a week" : 'Every weekday is closed. Set opening hours.');
        } catch (\PDOException $error) {
            $this->add('db.schema', 'fail', 'Database error: ' . $error->getMessage());
        }
    }

    private function checkPayment(): void
    {
        try {
            $mode = $this->config->paymentMode();
        } catch (\RuntimeException $error) {
            $this->add('payment.mode', 'fail', $error->getMessage());

            return;
        }
        if ($mode === 'none') {
            $this->add('payment.mode', 'warn', 'PAYMENT_MODE=none: customers pay at the venue. Set PAYMENT_MODE=square to charge cards online.');

            return;
        }
        try {
            $square = $this->config->square();
        } catch (\RuntimeException $error) {
            $this->add('payment.square', 'fail', $error->getMessage());

            return;
        }
        $this->add('payment.mode', 'pass', 'square (' . $square['environment'] . ')');
        if ($square['environment'] === 'sandbox' && $this->config->appEnv() === 'production') {
            $this->add('payment.square.environment', 'warn', 'SQUARE_ENV=sandbox while APP_ENV=production: no real money will be charged.');
        }
        if (!$this->online) {
            $this->add('payment.square.credentials', 'skip', 'Run doctor --online to verify the Square token and location.');

            return;
        }
        $base = $square['environment'] === 'production' ? 'https://connect.squareup.com' : 'https://connect.squareupsandbox.com';
        try {
            $response = $this->http->request('GET', $base . '/v2/locations/' . rawurlencode($square['locationId']), [
                'Authorization' => 'Bearer ' . $square['accessToken'],
                'Square-Version' => \ArcadeOS\Payments\SquareGateway::API_VERSION,
                'Accept' => 'application/json',
            ], null, 15);
            $this->add('payment.square.credentials', $response['status'] === 200 ? 'pass' : 'fail', $response['status'] === 200 ? 'token and location verified' : "Square answered HTTP {$response['status']}; check SQUARE_ACCESS_TOKEN, SQUARE_LOCATION_ID and SQUARE_ENV.");
        } catch (\RuntimeException $error) {
            $this->add('payment.square.credentials', 'fail', 'Could not reach Square: ' . $error->getMessage());
        }
    }

    private function checkRealtime(): void
    {
        try {
            $driver = $this->config->realtimeDriver();
            if ($driver === 'none') {
                $this->add('realtime.driver', 'pass', 'none (no station commands; fine without station clients)');

                return;
            }
            $this->config->pusher();
            $this->add('realtime.driver', 'pass', 'pusher configured');
        } catch (\RuntimeException $error) {
            $this->add('realtime.driver', 'fail', $error->getMessage());
        }
    }

    private function checkMail(): void
    {
        try {
            $driver = $this->config->mailDriver();
            if ($driver === 'log') {
                $this->add('mail.driver', 'warn', 'MAIL_DRIVER=log: confirmation emails are only written to storage/logs/mail.log. Use smtp or mail to send them.');

                return;
            }
            if (filter_var($this->config->mailFromAddress(), FILTER_VALIDATE_EMAIL) === false) {
                $this->add('mail.from', 'fail', 'MAIL_FROM_ADDRESS must be a valid email address.');
            }
            if ($driver === 'smtp') {
                $this->config->smtp();
            }
            $this->add('mail.driver', 'pass', $driver);
        } catch (\RuntimeException $error) {
            $this->add('mail.driver', 'fail', $error->getMessage());
        }
    }

    private function checkWeb(): void
    {
        if (!$this->online) {
            $this->add('web.exposure', 'skip', 'Run doctor --online after deploying to verify that private files are not web-accessible.');

            return;
        }
        $base = $this->config->appUrl();
        foreach (['/.env', '/src/Http/App.php', '/storage/logs/app.log', '/migrations/001_init.sql', '/composer.json'] as $private) {
            $status = $this->status($base . $private);
            if ($status === 0) {
                $this->add('web.private' . str_replace('/', '.', $private), 'fail', "Could not reach {$base}{$private} to verify it is private. Check APP_URL and that the site is up.");
                continue;
            }
            $this->add('web.private' . str_replace('/', '.', $private), $status === 200 ? 'fail' : 'pass', $status === 200 ? "{$private} is web-accessible. Point the document root at public/ or keep the root .htaccess." : "not served (HTTP {$status})");
        }
        $venue = $this->status($base . '/api/venue');
        $this->add('web.api', $venue === 200 ? 'pass' : 'fail', $venue === 200 ? 'the API answers' : "GET {$base}/api/venue returned HTTP {$venue}; check APP_URL and the rewrite rules.");
        $page = $this->status($base . '/book/');
        $this->add('web.booking_page', $page === 200 ? 'pass' : 'fail', $page === 200 ? 'served' : "GET {$base}/book/ returned HTTP {$page}; check the rewrite rules in public/.htaccess.");
    }

    private function status(string $url): int
    {
        try {
            return $this->http->request('GET', $url, ['Accept' => '*/*'], null, 15)['status'];
        } catch (\RuntimeException) {
            return 0;
        }
    }

    /** @return string[] */
    private function pendingMigrations(): array
    {
        if ($this->pdo === null) {
            return [];
        }
        $this->pdo->exec('CREATE TABLE IF NOT EXISTS migrations (version VARCHAR(100) NOT NULL PRIMARY KEY, applied_at DATETIME NOT NULL) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4');
        $query = $this->pdo->query('SELECT version FROM migrations');
        $done = $query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN);
        $pending = [];
        foreach (glob($this->migrationsDir . '/*.sql') ?: [] as $file) {
            $version = basename($file, '.sql');
            if (!in_array($version, $done, true)) {
                $pending[] = $version;
            }
        }

        return $pending;
    }

    private function count(string $table): int
    {
        $query = $this->pdo?->query("SELECT COUNT(*) FROM {$table}");

        return $query === false || $query === null ? 0 : (int) $query->fetchColumn();
    }

    private function add(string $check, string $status, string $detail): void
    {
        $this->results[] = ['check' => $check, 'status' => $status, 'detail' => $detail];
    }
}
