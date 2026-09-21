<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Domain;

use ArcadeOS\Domain\Block;
use ArcadeOS\Domain\StationAllocator;
use PHPUnit\Framework\TestCase;

final class StationAllocatorTest extends TestCase
{
    public function testReturnsNullWhenNotEnoughStationsAreFree(): void
    {
        self::assertNull(StationAllocator::choose([1], [], [1 => 1], 600, 2, 540));
    }

    public function testPrefersTheStationWithTheSmallestIdleGap(): void
    {
        $numbers = [1 => 1, 2 => 2, 3 => 3];
        $blocks = [new Block(2, 530, 590), new Block(3, 500, 560)];
        // gaps before 600: station 2 = 10, station 3 = 40, station 1 = 60 (idle since opening at 540)
        self::assertSame([2], StationAllocator::choose([1, 2, 3], $blocks, $numbers, 600, 1, 540));
        self::assertSame([2, 3], StationAllocator::choose([1, 2, 3], $blocks, $numbers, 600, 2, 540));
    }

    public function testTiesGoToTheLowestStationNumberNotTheLowestId(): void
    {
        $numbers = [10 => 2, 11 => 1];
        self::assertSame([11], StationAllocator::choose([10, 11], [], $numbers, 600, 1, 540));
    }

    public function testLaterBlocksDoNotAffectTheGap(): void
    {
        $numbers = [1 => 1, 2 => 2];
        $blocks = [new Block(2, 700, 760)];
        self::assertSame([1], StationAllocator::choose([1, 2], $blocks, $numbers, 600, 1, 540));
    }
}
