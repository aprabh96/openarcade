<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Domain\BookingRequest;
use ArcadeOS\Domain\HoursRepository;
use ArcadeOS\Domain\PriceRepository;
use ArcadeOS\Domain\StationRepository;
use ArcadeOS\Settings\SettingsRepository;
use PDO;

final class VenueFixture
{
    /** Chicago time, 9.35% tax, open 10:00-22:00 every day, 60 and 90 minute sessions, Saturday surcharge. */
    public static function seed(PDO $pdo, int $stations = 2): void
    {
        $settings = new SettingsRepository($pdo);
        $settings->set('timezone', 'America/Chicago');
        $settings->set('tax_rate_bp', '935');
        (new StationRepository($pdo))->syncCount($stations);
        $hours = new HoursRepository($pdo);
        for ($weekday = 0; $weekday <= 6; $weekday++) {
            $hours->setWeekday($weekday, 600, 1320, false);
        }
        $prices = new PriceRepository($pdo);
        $prices->set(-1, 60, 2500);
        $prices->set(-1, 90, 3550);
        $prices->set(6, 60, 3000);
    }

    public static function request(string $date, int $startMinute, int $duration = 60, int $stations = 1, string $email = 'alex.rivera@example.com'): BookingRequest
    {
        return new BookingRequest($date, $startMinute, $duration, $stations, 'Alex', 'Rivera', $email, '785-555-0142', null);
    }
}
