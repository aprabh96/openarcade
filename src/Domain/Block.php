<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

/** Time one station is taken on one local date, as minutes after local midnight. */
final class Block
{
    public function __construct(
        public readonly int $stationId,
        public readonly int $startMinute,
        public readonly int $endMinute,
    ) {
    }
}
