<?php

declare(strict_types=1);

namespace ArcadeOS\Console;

use ArcadeOS\Db\Migrator;
use ArcadeOS\Domain\HoursRepository;
use ArcadeOS\Domain\PriceRepository;
use ArcadeOS\Domain\StationRepository;
use ArcadeOS\Settings\SettingsRepository;
use ArcadeOS\Support\Clock;
use PDO;

/** First-time installation, shared by the console and the shared-hosting setup page. Safe to run twice. */
final class Installer
{
    public function __construct(private PDO $pdo, private string $migrationsDir, private Clock $clock)
    {
    }

    public static function validUsername(string $username): bool
    {
        return preg_match('/^[A-Za-z0-9_.\-]{3,50}$/', $username) === 1;
    }

    public static function validPassword(string $password): bool
    {
        return strlen($password) >= 12;
    }

    /**
     * @param array{venue?:string,timezone?:string,stations?:int} $options
     * @return string[] what happened, one line per step
     */
    public function install(string $adminUsername, string $adminPassword, array $options = []): array
    {
        if (!self::validUsername($adminUsername)) {
            throw new \InvalidArgumentException('The admin username must be 3-50 letters, digits, dot, dash or underscore.');
        }
        if (!self::validPassword($adminPassword)) {
            throw new \InvalidArgumentException('The admin password must be at least 12 characters.');
        }

        $lines = [];
        $applied = (new Migrator($this->pdo, $this->migrationsDir))->migrate();
        $lines[] = $applied === [] ? 'Database is up to date.' : 'Applied: ' . implode(', ', $applied);

        $settings = new SettingsRepository($this->pdo);
        if (isset($options['venue'])) {
            $settings->set('venue_name', $options['venue']);
        }
        if (isset($options['timezone'])) {
            $settings->set('timezone', $options['timezone']);
        }

        $stations = new StationRepository($this->pdo);
        if ($stations->activeNumbersById() === [] || isset($options['stations'])) {
            $stations->syncCount($options['stations'] ?? 4);
        }

        if ($this->isEmpty('business_hours')) {
            $hours = new HoursRepository($this->pdo);
            for ($weekday = 0; $weekday <= 6; $weekday++) {
                $hours->setWeekday($weekday, 600, 1320, false);
            }
            $lines[] = 'Default opening hours set: 10:00 to 22:00 every day.';
        }
        if ($this->isEmpty('prices')) {
            $prices = new PriceRepository($this->pdo);
            $prices->set(-1, 60, 2500);
            $prices->set(-1, 90, 3500);
            $prices->set(-1, 120, 4500);
            $lines[] = 'Default prices set: 60, 90 and 120 minute sessions.';
        }

        $lines[] = $this->createAdmin($adminUsername, $adminPassword);

        return $lines;
    }

    public function createAdmin(string $username, string $password): string
    {
        if (!self::validUsername($username)) {
            throw new \InvalidArgumentException('The admin username must be 3-50 letters, digits, dot, dash or underscore.');
        }
        if (!self::validPassword($password)) {
            throw new \InvalidArgumentException('The admin password must be at least 12 characters.');
        }
        $exists = $this->pdo->prepare('SELECT 1 FROM admins WHERE username = ?');
        $exists->execute([$username]);
        if ($exists->fetchColumn() !== false) {
            return "Admin {$username} already exists; left unchanged.";
        }
        $this->pdo->prepare('INSERT INTO admins (username, password_hash, created_at) VALUES (?, ?, ?)')
            ->execute([$username, password_hash($password, PASSWORD_DEFAULT), $this->clock->now()->format('Y-m-d H:i:s')]);

        return "Admin {$username} created.";
    }

    public function hasAdmin(): bool
    {
        try {
            $query = $this->pdo->query('SELECT COUNT(*) FROM admins');
        } catch (\PDOException) {
            return false;
        }

        return $query !== false && (int) $query->fetchColumn() > 0;
    }

    private function isEmpty(string $table): bool
    {
        $query = $this->pdo->query("SELECT COUNT(*) FROM {$table}");

        return $query === false || (int) $query->fetchColumn() === 0;
    }
}
