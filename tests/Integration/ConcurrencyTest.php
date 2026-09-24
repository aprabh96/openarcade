<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\BookingRules;
use OpenArcade\Domain\Reservations;
use OpenArcade\Support\FixedClock;
use PHPUnit\Framework\TestCase;

final class ConcurrencyTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    /**
     * Starts one process per element of $argsPerWorker, all running $worker, all released at the
     * same instant so they race against each other for real.
     *
     * @param list<list<string>> $argsPerWorker one argument list per worker, appended after the shared start time
     * @return array<int, array<string, mixed>> decoded worker outputs
     */
    private function race(string $worker, array $argsPerWorker): array
    {
        $startAt = sprintf('%.4F', microtime(true) + 1.5);
        $processes = [];
        foreach ($argsPerWorker as $args) {
            $pipes = [];
            $process = proc_open(
                [PHP_BINARY, __DIR__ . '/workers/' . $worker, $startAt, ...$args],
                [1 => ['pipe', 'w'], 2 => ['pipe', 'w']],
                $pipes
            );
            self::assertIsResource($process);
            $processes[] = [$process, $pipes];
        }
        $results = [];
        foreach ($processes as [$process, $pipes]) {
            $out = stream_get_contents($pipes[1]);
            $err = stream_get_contents($pipes[2]);
            fclose($pipes[1]);
            fclose($pipes[2]);
            proc_close($process);
            $decoded = json_decode((string) $out, true);
            self::assertIsArray($decoded, 'worker output: ' . $out . ' stderr: ' . $err);
            $results[] = $decoded;
        }

        return $results;
    }

    public function testEightWritersForOneStationExactlyOneWins(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 1);
        $results = $this->race('book_worker.php', array_fill(0, 8, ['2026-09-22', '600', '1']));
        $outcomes = array_count_values(array_column($results, 'result'));
        self::assertSame(
            ['booked' => 1, 'slot_unavailable' => 7],
            ['booked' => $outcomes['booked'] ?? 0, 'slot_unavailable' => $outcomes['slot_unavailable'] ?? 0],
            json_encode($results) ?: ''
        );
        $count = $pdo->query("SELECT COUNT(*) FROM reservations WHERE status = 'confirmed'");
        self::assertSame(1, $count === false ? -1 : (int) $count->fetchColumn());
    }

    public function testEightWritersForThreeStationsNeverShareAStation(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 3);
        $results = $this->race('book_worker.php', array_fill(0, 8, ['2026-09-22', '600', '1']));
        $booked = array_values(array_filter($results, static fn (array $r): bool => $r['result'] === 'booked'));
        self::assertCount(3, $booked, json_encode($results) ?: '');
        $stations = array_merge(...array_column($booked, 'stations'));
        sort($stations);
        self::assertSame(array_values(array_unique($stations)), $stations, 'a station was given to two bookings');
        self::assertCount(3, $stations);
    }

    public function testGroupsWantingTwoOfThreeStationsOnlyOneFits(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 3);
        $results = $this->race('book_worker.php', array_fill(0, 6, ['2026-09-22', '600', '2']));
        $outcomes = array_count_values(array_column($results, 'result'));
        self::assertSame(1, $outcomes['booked'] ?? 0, json_encode($results) ?: '');
        self::assertSame(5, $outcomes['slot_unavailable'] ?? 0, json_encode($results) ?: '');
    }

    public function testSixDuplicateConfirmationsAreAllIdempotent(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 1);
        $service = Reservations::build($pdo, new FixedClock('2026-09-21 14:00:00'));
        $held = $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));

        $results = $this->race('confirm_worker.php', array_fill(0, 6, [(string) $held->id, 'pay_same']));

        foreach ($results as $result) {
            self::assertSame('confirmed', $result['result'], json_encode($results) ?: '');
            self::assertSame('pay_same', $result['payment_id'], json_encode($results) ?: '');
        }
        $status = $pdo->query('SELECT status FROM reservations WHERE id = ' . $held->id);
        self::assertSame('confirmed', $status === false ? null : $status->fetchColumn());
        $paymentId = $pdo->query('SELECT payment_id FROM reservations WHERE id = ' . $held->id);
        self::assertSame('pay_same', $paymentId === false ? null : $paymentId->fetchColumn());
    }

    public function testSixDifferentPaymentIdsExactlyOneWins(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 1);
        $service = Reservations::build($pdo, new FixedClock('2026-09-21 14:00:00'));
        $held = $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));

        $args = array_map(static fn (int $i): array => [(string) $held->id, "pay_{$i}"], range(0, 5));
        $results = $this->race('confirm_worker.php', $args);

        $outcomes = array_count_values(array_column($results, 'result'));
        self::assertSame(
            ['confirmed' => 1, 'wrong_status' => 5, 'error' => 0],
            [
                'confirmed' => $outcomes['confirmed'] ?? 0,
                'wrong_status' => $outcomes['wrong_status'] ?? 0,
                'error' => $outcomes['error'] ?? 0,
            ],
            json_encode($results) ?: ''
        );

        $winners = array_values(array_filter($results, static fn (array $r): bool => $r['result'] === 'confirmed'));
        self::assertCount(1, $winners, json_encode($results) ?: '');
        $paymentId = $pdo->query('SELECT payment_id FROM reservations WHERE id = ' . $held->id);
        self::assertSame($winners[0]['payment_id'], $paymentId === false ? null : $paymentId->fetchColumn());
    }
}
