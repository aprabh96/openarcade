<?php

declare(strict_types=1);

use ArcadeOS\Domain\BookingRejected;
use ArcadeOS\Domain\BookingRules;
use ArcadeOS\Domain\Reservations;
use ArcadeOS\Support\FixedClock;
use ArcadeOS\Tests\Integration\TestDb;
use ArcadeOS\Tests\Integration\VenueFixture;

require dirname(__DIR__, 3) . '/vendor/autoload.php';

[, $startAt, $date, $startMinute, $stations] = $argv;

$service = Reservations::build(TestDb::connect(), new FixedClock('2026-09-21 14:00:00'));
while (microtime(true) < (float) $startAt) {
    usleep(500);
}
try {
    $reservation = $service->create(VenueFixture::request($date, (int) $startMinute, 60, (int) $stations), BookingRules::customer(false));
    echo json_encode(['result' => 'booked', 'stations' => $reservation->stationIds]);
} catch (BookingRejected $rejected) {
    echo json_encode(['result' => $rejected->reason]);
} catch (\Throwable $error) {
    echo json_encode(['result' => 'error', 'message' => $error->getMessage()]);
}
