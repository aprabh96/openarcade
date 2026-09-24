<?php

declare(strict_types=1);

namespace OpenArcade\Support;

final class FixedClock implements Clock
{
    private \DateTimeImmutable $now;

    public function __construct(string $utcDateTime)
    {
        $this->now = new \DateTimeImmutable($utcDateTime, new \DateTimeZone('UTC'));
    }

    public function now(): \DateTimeImmutable
    {
        return $this->now;
    }

    public function advanceMinutes(int $minutes): void
    {
        $this->now = $this->now->modify(sprintf('%+d minutes', $minutes));
    }
}
