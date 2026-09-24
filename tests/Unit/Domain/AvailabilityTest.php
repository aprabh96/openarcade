<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Domain;

use OpenArcade\Domain\Availability;
use OpenArcade\Domain\Block;
use OpenArcade\Domain\DayHours;
use OpenArcade\Domain\Slot;
use PHPUnit\Framework\TestCase;

final class AvailabilityTest extends TestCase
{
    /**
     * @param Slot[] $slots
     * @return array<int, int[]> start minute => free station ids
     */
    private function asMap(array $slots): array
    {
        $map = [];
        foreach ($slots as $slot) {
            $map[$slot->startMinute] = $slot->freeStationIds;
        }

        return $map;
    }

    public function testGridCoversOpeningHoursAndLastSlotEndsAtClose(): void
    {
        $slots = Availability::slots(new DayHours(600, 720), [1, 2], [], 60, 30, 0);
        self::assertSame([600 => [1, 2], 630 => [1, 2], 660 => [1, 2]], $this->asMap($slots));
    }

    public function testBlockWithBufferRemovesStationFromConflictingSlots(): void
    {
        $blocks = [new Block(1, 630, 690)];
        $map = $this->asMap(Availability::slots(new DayHours(600, 780), [1, 2], $blocks, 60, 30, 10));
        self::assertSame([2], $map[600]);
        self::assertSame([2], $map[630]);
        self::assertSame([2], $map[660]);
        self::assertSame([2], $map[690], 'starts inside the 10 minute buffer after the block');
        self::assertSame([1, 2], $map[720]);
    }

    public function testBackToBackIsAllowedOnlyWithZeroBuffer(): void
    {
        $blocks = [new Block(1, 600, 660)];
        self::assertSame([1], Availability::freeStations([1], $blocks, 660, 720, 0));
        self::assertSame([], Availability::freeStations([1], $blocks, 660, 720, 10));
        self::assertSame([1], Availability::freeStations([1], $blocks, 670, 730, 10));
    }

    public function testBufferAlsoAppliesBeforeAnExistingBlock(): void
    {
        $blocks = [new Block(1, 700, 760)];
        self::assertSame([], Availability::freeStations([1], $blocks, 635, 695, 10));
        self::assertSame([1], Availability::freeStations([1], $blocks, 630, 690, 10));
    }

    public function testEarliestStartIsRoundedUpToTheGrid(): void
    {
        $slots = Availability::slots(new DayHours(600, 780), [1], [], 60, 30, 0, 645);
        self::assertSame([660, 690, 720], array_keys($this->asMap($slots)));
    }

    public function testDurationLongerThanTheDayGivesNoSlots(): void
    {
        self::assertSame([], Availability::slots(new DayHours(600, 660), [1], [], 90, 30, 0));
    }

    public function testBlocksForUnknownStationsAreIgnored(): void
    {
        $slots = Availability::slots(new DayHours(600, 660), [1], [new Block(99, 600, 660)], 60, 30, 0);
        self::assertSame([600 => [1]], $this->asMap($slots));
    }

    public function testRejectsNonPositiveStepOrDuration(): void
    {
        foreach ([[0, 30], [60, 0]] as [$duration, $step]) {
            try {
                Availability::slots(new DayHours(600, 720), [1], [], $duration, $step, 0);
                self::fail('expected InvalidArgumentException');
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }
}
