<?php

declare(strict_types=1);

use OpenArcade\Domain\BookingRejected;
use OpenArcade\Domain\Reservations;
use OpenArcade\Support\FixedClock;
use OpenArcade\Tests\Integration\TestDb;

require dirname(__DIR__, 3) . '/vendor/autoload.php';

[, $startAt, $reservationId, $paymentId] = $argv;

$service = Reservations::build(TestDb::connect(), new FixedClock('2026-09-21 14:00:00'));
while (microtime(true) < (float) $startAt) {
    usleep(500);
}
try {
    $reservation = $service->confirmPayment((int) $reservationId, 'square', $paymentId);
    echo json_encode(['result' => $reservation->status, 'payment_id' => $reservation->paymentId]);
} catch (BookingRejected $rejected) {
    echo json_encode(['result' => $rejected->reason]);
} catch (\Throwable $error) {
    echo json_encode(['result' => 'error', 'message' => $error->getMessage()]);
}
