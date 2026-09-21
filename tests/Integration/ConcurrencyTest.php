<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use PHPUnit\Framework\TestCase;

final class ConcurrencyTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    /** @return array<int, array<string, mixed>> decoded worker outputs */
    private function race(int $workers, string $date, int $startMinute, int $stationsEach): array
    {
        $startAt = sprintf('%.4F', microtime(true) + 1.5);
        $processes = [];
        for ($i = 0; $i < $workers; $i++) {
            $pipes = [];
            $process = proc_open(
                [PHP_BINARY, __DIR__ . '/workers/book_worker.php', $startAt, $date, (string) $startMinute, (string) $stationsEach],
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
        $results = $this->race(8, '2026-09-22', 600, 1);
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
        $results = $this->race(8, '2026-09-22', 600, 1);
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
        $results = $this->race(6, '2026-09-22', 600, 2);
        $outcomes = array_count_values(array_column($results, 'result'));
        self::assertSame(1, $outcomes['booked'] ?? 0, json_encode($results) ?: '');
        self::assertSame(5, $outcomes['slot_unavailable'] ?? 0, json_encode($results) ?: '');
    }
}
