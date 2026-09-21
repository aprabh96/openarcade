<?php

declare(strict_types=1);

namespace ArcadeOS\Settings;

use PDO;

final class SettingsRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    /** @return array<string,string> */
    public function all(): array
    {
        $query = $this->pdo->query('SELECT `key`, `value` FROM settings');
        $rows = $query === false ? [] : $query->fetchAll(PDO::FETCH_KEY_PAIR);

        return array_map('strval', $rows);
    }

    public function load(): VenueSettings
    {
        return VenueSettings::fromArray($this->all());
    }

    public function set(string $key, string $value): void
    {
        if (!array_key_exists($key, VenueSettings::DEFAULTS)) {
            throw new \InvalidArgumentException("Unknown setting {$key}.");
        }
        // Validate the whole set with the new value before saving it.
        VenueSettings::fromArray(array_merge($this->all(), [$key => $value]));
        $this->pdo->prepare('INSERT INTO settings (`key`, `value`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `value` = VALUES(`value`)')
            ->execute([$key, $value]);
    }
}
