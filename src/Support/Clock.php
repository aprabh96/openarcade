<?php

declare(strict_types=1);

namespace ArcadeOS\Support;

interface Clock
{
    /** Current instant, always in UTC. */
    public function now(): \DateTimeImmutable;
}
