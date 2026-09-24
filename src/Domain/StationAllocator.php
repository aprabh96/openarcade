<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class StationAllocator
{
    /**
     * @param int[] $freeStationIds stations free for the requested window
     * @param Block[] $blocks all blocking sessions on that date
     * @param array<int,int> $numbersById station id => station number
     * @return int[]|null chosen station ids ordered by station number, or null when too few are free
     */
    public static function choose(
        array $freeStationIds,
        array $blocks,
        array $numbersById,
        int $startMinute,
        int $count,
        int $openMinute,
    ): ?array {
        if ($count < 1 || count($freeStationIds) < $count) {
            return null;
        }

        $candidates = [];
        foreach ($freeStationIds as $id) {
            $previousEnd = min($openMinute, $startMinute);
            foreach ($blocks as $block) {
                if ($block->stationId === $id && $block->endMinute <= $startMinute && $block->endMinute > $previousEnd) {
                    $previousEnd = $block->endMinute;
                }
            }
            $candidates[] = [
                'id' => $id,
                'gap' => $startMinute - $previousEnd,
                'number' => $numbersById[$id] ?? PHP_INT_MAX,
            ];
        }
        usort($candidates, static fn (array $a, array $b): int => [$a['gap'], $a['number']] <=> [$b['gap'], $b['number']]);

        $chosen = array_slice($candidates, 0, $count);
        usort($chosen, static fn (array $a, array $b): int => $a['number'] <=> $b['number']);

        return array_column($chosen, 'id');
    }
}
