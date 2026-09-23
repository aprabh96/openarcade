<?php

declare(strict_types=1);

namespace ArcadeOS\Console;

use ArcadeOS\Db\Migrator;
use ArcadeOS\Domain\BookingRejected;
use ArcadeOS\Domain\BookingRequest;
use ArcadeOS\Domain\BookingRules;
use ArcadeOS\Domain\Reservations;
use ArcadeOS\Payments\NullGateway;
use ArcadeOS\Payments\PaymentGateway;
use ArcadeOS\Settings\SettingsRepository;
use ArcadeOS\Support\Clock;
use ArcadeOS\Support\Logger;
use PDO;

final class Application
{
    private const DEMO_PEOPLE = [
        ['Alex', 'Rivera'], ['Sam', 'Okafor'], ['Jordan', 'Lee'], ['Taylor', 'Novak'], ['Morgan', 'Diaz'],
        ['Casey', 'Nguyen'], ['Riley', 'Patel'], ['Jamie', 'Kowalski'], ['Avery', 'Santos'], ['Quinn', 'Haddad'],
    ];

    /** @var callable(string): void */
    private $write;

    private PaymentGateway $gateway;
    private Logger $logger;

    /** @param callable(string): void $write */
    public function __construct(
        private PDO $pdo,
        private string $migrationsDir,
        private Clock $clock,
        callable $write,
        ?PaymentGateway $gateway = null,
        ?Logger $logger = null,
    ) {
        $this->write = $write;
        $this->gateway = $gateway ?? new NullGateway();
        $this->logger = $logger ?? new Logger(null);
    }

    /** @param string[] $argv */
    public function run(array $argv): int
    {
        $command = $argv[1] ?? 'help';
        $options = [];
        foreach (array_slice($argv, 2) as $arg) {
            if (preg_match('/^--([a-z][a-z0-9\-]*)(?:=(.*))?$/', $arg, $m) === 1) {
                $options[$m[1]] = $m[2] ?? '1';
            }
        }

        try {
            return match ($command) {
                'migrate' => $this->migrate(),
                'install' => $this->install($options),
                'admin:create' => $this->createAdmin($options),
                'admin:unlock' => $this->unlockAdmin($options),
                'seed:demo' => $this->seedDemo($options),
                'holds:release' => $this->releaseHolds(),
                'privacy:purge' => $this->purge($options),
                'help' => $this->help(0),
                default => $this->help(1),
            };
        } catch (\InvalidArgumentException $error) {
            ($this->write)('Error: ' . $error->getMessage());

            return 1;
        } catch (\PDOException $error) {
            ($this->write)('Database error: ' . $error->getMessage());

            return 1;
        }
    }

    private function help(int $exitCode): int
    {
        foreach ([
            'Usage: php bin/console <command> [--option=value]',
            '  migrate        Apply database migrations',
            '  install        Migrate, seed defaults, create the first admin',
            '                 --admin-user= --admin-password= [--stations=4] [--venue=] [--timezone=]',
            '                 (the password may come from the ARCADEOS_ADMIN_PASSWORD environment variable)',
            '  admin:create   Create another admin: --admin-user= --admin-password=',
            '  admin:unlock   Clear failed sign-in attempts for a username: --admin-user=',
            '  seed:demo      Add fake reservations for demos and screenshots (refused when real bookings exist unless --force)',
            '  holds:release  Release unpaid holds (checks the payment provider first when PAYMENT_MODE is not none)',
            '  privacy:purge  Anonymise guest details of past reservations: --older-than-months=12',
            '  doctor         Check the installation: [--online] [--json]. Exit code 1 when anything fails.',
        ] as $line) {
            ($this->write)($line);
        }

        return $exitCode;
    }

    private function migrate(): int
    {
        $applied = (new Migrator($this->pdo, $this->migrationsDir))->migrate();
        ($this->write)($applied === [] ? 'Database is up to date.' : 'Applied: ' . implode(', ', $applied));

        return 0;
    }

    /** @param array<string,string> $options */
    private function install(array $options): int
    {
        // Validate the admin first so a bad password changes nothing.
        $username = trim($options['admin-user'] ?? '');
        if (!Installer::validUsername($username)) {
            throw new \InvalidArgumentException('--admin-user must be 3-50 letters, digits, dot, dash or underscore.');
        }
        $password = $this->adminPassword($options);

        $installOptions = [];
        if (isset($options['venue'])) {
            $installOptions['venue'] = $options['venue'];
        }
        if (isset($options['timezone'])) {
            $installOptions['timezone'] = $options['timezone'];
        }
        if (isset($options['stations'])) {
            $installOptions['stations'] = (int) $options['stations'];
        }
        foreach ($this->installer()->install($username, $password, $installOptions) as $line) {
            ($this->write)($line);
        }
        ($this->write)('Installed. Change hours, prices and stations in the dashboard settings.');

        return 0;
    }

    /** @param array<string,string> $options */
    private function createAdmin(array $options): int
    {
        ($this->write)($this->installer()->createAdmin(trim($options['admin-user'] ?? ''), $this->adminPassword($options)));

        return 0;
    }

    /** @param array<string,string> $options */
    private function unlockAdmin(array $options): int
    {
        $username = trim($options['admin-user'] ?? '');
        if ($username === '') {
            throw new \InvalidArgumentException('--admin-user is required.');
        }
        $removed = (new \ArcadeOS\Auth\LoginThrottle($this->pdo))->clear($username);
        ($this->write)("Cleared {$removed} failed sign-in attempt(s) for {$username}.");

        return 0;
    }

    private function installer(): Installer
    {
        return new Installer($this->pdo, $this->migrationsDir, $this->clock);
    }

    /** @param array<string,string> $options */
    private function seedDemo(array $options): int
    {
        $query = $this->pdo->query("SELECT COUNT(*) FROM reservations WHERE comments IS NULL OR comments <> 'Demo booking'");
        $real = $query === false ? 0 : (int) $query->fetchColumn();
        if ($real > 0 && !isset($options['force'])) {
            throw new \InvalidArgumentException("The database already holds {$real} real reservation(s); demo bookings would land in the live calendar. Use --force only on a test copy.");
        }
        $service = Reservations::build($this->pdo, $this->clock);
        $tz = (new SettingsRepository($this->pdo))->load()->tz();
        $today = $this->clock->now()->setTimezone($tz);
        $created = 0;
        foreach (self::DEMO_PEOPLE as $index => [$first, $last]) {
            $request = new BookingRequest(
                $today->modify('+' . (1 + $index % 3) . ' days')->format('Y-m-d'),
                600 + ($index % 5) * 90,
                60,
                1 + $index % 2,
                $first,
                $last,
                strtolower($first . '.' . $last) . '@example.com',
                sprintf('785-555-01%02d', $index + 10),
                'Demo booking',
            );
            try {
                $service->create($request, BookingRules::admin());
                $created++;
            } catch (BookingRejected) {
                continue;
            }
        }
        ($this->write)("Created {$created} demo reservations.");

        return 0;
    }

    private function releaseHolds(): int
    {
        $report = Reservations::build($this->pdo, $this->clock)->reconcileHolds($this->gateway, $this->logger);
        ($this->write)(sprintf(
            'Holds: %d expired, %d confirmed from late payments, %d refunded, %d kept for the next run.',
            $report['expired'],
            $report['confirmed'],
            $report['refunded'],
            $report['kept'],
        ));

        return 0;
    }

    /** @param array<string,string> $options */
    private function purge(array $options): int
    {
        $months = (int) ($options['older-than-months'] ?? 0);
        if ($months < 1 || $months > 120) {
            throw new \InvalidArgumentException('--older-than-months must be from 1 to 120.');
        }
        $tz = (new SettingsRepository($this->pdo))->load()->tz();
        $cutoff = $this->clock->now()->setTimezone($tz)->modify("-{$months} months")->format('Y-m-d');
        $count = (new \ArcadeOS\Domain\ReservationRepository($this->pdo))->anonymiseBefore($cutoff, $this->clock->now());
        ($this->write)("Anonymised {$count} reservation(s) dated before {$cutoff}.");

        return 0;
    }

    /** @param array<string,string> $options */
    private function adminPassword(array $options): string
    {
        $password = $options['admin-password'] ?? (getenv('ARCADEOS_ADMIN_PASSWORD') ?: '');
        if ($password === '' && defined('STDIN') && function_exists('posix_isatty') && posix_isatty(STDIN)) {
            ($this->write)('Admin password (12+ characters, input hidden):');
            $saved = shell_exec('stty -g 2>/dev/null');
            $hidden = is_string($saved) && trim($saved) !== '';
            if ($hidden) {
                shell_exec('stty -echo');
            }
            try {
                $password = trim((string) fgets(STDIN));
            } finally {
                if ($hidden) {
                    shell_exec('stty ' . escapeshellarg(trim((string) $saved)));
                    ($this->write)('');
                }
            }
        }
        if (!Installer::validPassword($password)) {
            throw new \InvalidArgumentException('The admin password must be at least 12 characters.');
        }

        return $password;
    }
}
