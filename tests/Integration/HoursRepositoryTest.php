<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\HoursRepository;
use PHPUnit\Framework\TestCase;

final class HoursRepositoryTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testPrecedenceClosedDateThenSpecialHoursThenWeekday(): void
    {
        $pdo = TestDb::fresh();
        $repo = new HoursRepository($pdo);
        // 2026-09-21 is a Monday (weekday 1), 2026-09-22 a Tuesday (2), 2026-09-23 a Wednesday (3).
        $repo->setWeekday(1, 600, 1320, false);
        $repo->setWeekday(2, 600, 1320, true);
        $repo->setWeekday(3, 600, 1320, false);
        $repo->setSpecialHours('2026-09-23', 720, 1080);
        $repo->addClosedDate('2026-09-28', 'Private event');
        $repo->setWeekday(0, 600, 1320, false);

        $monday = $repo->forDate('2026-09-21');
        self::assertNotNull($monday);
        self::assertSame([600, 1320], [$monday->openMinute, $monday->closeMinute]);
        self::assertNull($repo->forDate('2026-09-22'), 'weekday marked closed');
        $special = $repo->forDate('2026-09-23');
        self::assertNotNull($special);
        self::assertSame([720, 1080], [$special->openMinute, $special->closeMinute]);
        self::assertNull($repo->forDate('2026-09-28'), 'closed date wins over weekday hours');
        self::assertNull($repo->forDate('2026-09-24'), 'no row for Thursday means closed');
    }

    public function testRejectsMalformedDate(): void
    {
        $this->expectException(\InvalidArgumentException::class);
        (new HoursRepository(TestDb::fresh()))->forDate('09/21/2026');
    }
}
