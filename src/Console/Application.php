<?php

declare(strict_types=1);

namespace ArcadeOS\Console;

use ArcadeOS\Db\Migrator;
use ArcadeOS\Domain\BookingRejected;
use ArcadeOS\Domain\BookingRequest;
use ArcadeOS\Domain\BookingRules;
use ArcadeOS\Domain\HoursRepository;
use ArcadeOS\Domain\PriceRepository;
use ArcadeOS\Domain\Reservations;
use ArcadeOS\Domain\StationRepository;
use ArcadeOS\Settings\SettingsRepository;
use ArcadeOS\Support\Clock;
use PDO;

final class Application
{
    private const DEMO_PEOPLE = [
        ['Alex', 'Rivera'], ['Sam', 'Okafor'], ['Jordan', 'Lee'], ['Taylor', 'Novak'], ['Morgan', 'Diaz'],
        ['Casey', 'Nguyen'], ['Riley', 'Patel'], ['Jamie', 'Kowalski'], ['Avery', 'Santos'], ['Quinn', 'Haddad'],
    ];

    /** @var callable(string): void */
    private $write;

    /** @param callable(string): void $write */
    public function __construct(private PDO $pdo, private string $migrationsDir, private Clock $clock, callable $write)
    {
        $this->write = $write;
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
                'seed:demo' => $this->seedDemo(),
                'holds:release' => $this->releaseHolds(),
                'help' => $this->help(0),
                default => $this->help(1),
            };
        } catch (\InvalidArgumentException $error) {
            ($this->write)('Error: ' . $error->getMessage());

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
            '  seed:demo      Add fake reservations for demos and screenshots',
            '  holds:release  Expire unpaid holds',
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
        $username = $this->adminUsername($options);
        $password = $this->adminPassword($options);

        $this->migrate();

        $settings = new SettingsRepository($this->pdo);
        if (isset($options['venue'])) {
            $settings->set('venue_name', $options['venue']);
        }
        if (isset($options['timezone'])) {
            $settings->set('timezone', $options['timezone']);
        }

        $stations = new StationRepository($this->pdo);
        if ($stations->activeNumbersById() === [] || isset($options['stations'])) {
            $stations->syncCount((int) ($options['stations'] ?? 4));
        }

        if ($this->isEmpty('business_hours')) {
            $hours = new HoursRepository($this->pdo);
            for ($weekday = 0; $weekday <= 6; $weekday++) {
                $hours->setWeekday($weekday, 600, 1320, false);
            }
        }
        if ($this->isEmpty('prices')) {
            $prices = new PriceRepository($this->pdo);
            $prices->set(-1, 60, 2500);
            $prices->set(-1, 90, 3500);
            $prices->set(-1, 120, 4500);
        }

        $this->insertAdmin($username, $password);
        ($this->write)('Installed. Change hours, prices and stations in the dashboard settings.');

        return 0;
    }

    /** @param array<string,string> $options */
    private function createAdmin(array $options): int
    {
        $this->insertAdmin($this->adminUsername($options), $this->adminPassword($options));

        return 0;
    }

    private function seedDemo(): int
    {
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
        $count = Reservations::build($this->pdo, $this->clock)->expireHolds();
        ($this->write)("Expired {$count} unpaid hold(s).");

        return 0;
    }

    private function isEmpty(string $table): bool
    {
        $query = $this->pdo->query("SELECT COUNT(*) FROM {$table}");

        return $query === false || (int) $query->fetchColumn() === 0;
    }

    /** @param array<string,string> $options */
    private function adminUsername(array $options): string
    {
        $username = trim($options['admin-user'] ?? '');
        if (preg_match('/^[A-Za-z0-9_.\-]{3,50}$/', $username) !== 1) {
            throw new \InvalidArgumentException('--admin-user must be 3-50 letters, digits, dot, dash or underscore.');
        }

        return $username;
    }

    /** @param array<string,string> $options */
    private function adminPassword(array $options): string
    {
        $password = $options['admin-password'] ?? (getenv('ARCADEOS_ADMIN_PASSWORD') ?: '');
        if ($password === '' && defined('STDIN') && function_exists('posix_isatty') && posix_isatty(STDIN)) {
            ($this->write)('Admin password (12+ characters):');
            $password = trim((string) fgets(STDIN));
        }
        if (strlen($password) < 12) {
            throw new \InvalidArgumentException('The admin password must be at least 12 characters.');
        }

        return $password;
    }

    private function insertAdmin(string $username, string $password): void
    {
        $exists = $this->pdo->prepare('SELECT 1 FROM admins WHERE username = ?');
        $exists->execute([$username]);
        if ($exists->fetchColumn() !== false) {
            ($this->write)("Admin {$username} already exists; left unchanged.");

            return;
        }
        $this->pdo->prepare('INSERT INTO admins (username, password_hash, created_at) VALUES (?, ?, ?)')
            ->execute([$username, password_hash($password, PASSWORD_DEFAULT), $this->clock->now()->format('Y-m-d H:i:s')]);
        ($this->write)("Admin {$username} created.");
    }
}
