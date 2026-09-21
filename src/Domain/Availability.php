<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

final class Availability
{
    /**
     * Every start time on the slot grid that fits in the opening hours, with the stations free for it.
     *
     * @param int[] $stationIds active station ids
     * @param Block[] $blocks blocking sessions on that date
     * @return Slot[]
     */
    public static function slots(
        DayHours $hours,
        array $stationIds,
        array $blocks,
        int $durationMinutes,
        int $slotStepMinutes,
        int $bufferMinutes,
        int $earliestStartMinute = 0,
    ): array {
        if ($durationMinutes < 1 || $slotStepMinutes < 1 || $bufferMinutes < 0) {
            throw new \InvalidArgumentException('Duration and slot step must be positive; buffer cannot be negative.');
        }

        $first = $hours->openMinute;
        if ($earliestStartMinute > $first) {
            $stepsToSkip = intdiv($earliestStartMinute - $first + $slotStepMinutes - 1, $slotStepMinutes);
            $first += $stepsToSkip * $slotStepMinutes;
        }

        $slots = [];
        for ($start = $first; $start + $durationMinutes <= $hours->closeMinute; $start += $slotStepMinutes) {
            $slots[] = new Slot(
                $start,
                self::freeStations($stationIds, $blocks, $start, $start + $durationMinutes, $bufferMinutes)
            );
        }

        return $slots;
    }

    /**
     * Stations with no blocking session within $bufferMinutes of [$startMinute, $endMinute).
     *
     * @param int[] $stationIds
     * @param Block[] $blocks
     * @return int[] in the order given
     */
    public static function freeStations(array $stationIds, array $blocks, int $startMinute, int $endMinute, int $bufferMinutes): array
    {
        $taken = [];
        foreach ($blocks as $block) {
            if ($startMinute < $block->endMinute + $bufferMinutes && $block->startMinute < $endMinute + $bufferMinutes) {
                $taken[$block->stationId] = true;
            }
        }

        return array_values(array_filter($stationIds, static fn (int $id): bool => !isset($taken[$id])));
    }
}
