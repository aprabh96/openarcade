<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Support;

use ArcadeOS\Support\FixedClock;
use ArcadeOS\Support\SystemClock;
use PHPUnit\Framework\TestCase;

final class ClockTest extends TestCase
{
    public function testFixedClockReturnsUtcInstantAndCanAdvance(): void
    {
        $clock = new FixedClock('2026-03-08 15:00:00');
        self::assertSame('2026-03-08 15:00:00 UTC', $clock->now()->format('Y-m-d H:i:s T'));
        $clock->advanceMinutes(90);
        self::assertSame('2026-03-08 16:30:00', $clock->now()->format('Y-m-d H:i:s'));
    }

    public function testSystemClockIsUtc(): void
    {
        self::assertSame('UTC', (new SystemClock())->now()->getTimezone()->getName());
    }
}
