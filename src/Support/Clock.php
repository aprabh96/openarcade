<?php

declare(strict_types=1);

namespace OpenArcade\Support;

interface Clock
{
    /** Current instant, always in UTC. */
    public function now(): \DateTimeImmutable;
}
