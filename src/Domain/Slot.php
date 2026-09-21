<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

final class Slot
{
    /** @param int[] $freeStationIds */
    public function __construct(public readonly int $startMinute, public readonly array $freeStationIds)
    {
    }
}
