<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

/** Opening hours for one local date, as minutes after local midnight. Sessions never cross midnight. */
final class DayHours
{
    public function __construct(public readonly int $openMinute, public readonly int $closeMinute)
    {
        if ($openMinute < 0 || $closeMinute > 1440 || $openMinute >= $closeMinute) {
            throw new \InvalidArgumentException('Hours must satisfy 0 <= open < close <= 1440.');
        }
    }
}
