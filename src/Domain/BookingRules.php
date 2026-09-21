<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

final class BookingRules
{
    private function __construct(
        public readonly bool $enforceHours,
        public readonly bool $enforceGrid,
        public readonly bool $enforceLeadTime,
        public readonly bool $enforceAdvanceLimit,
        public readonly bool $contactRequired,
        public readonly bool $requirePayment,
        public readonly bool $complimentary,
        public readonly string $createdBy,
    ) {
    }

    /** A booking made by the public. */
    public static function customer(bool $requirePayment): self
    {
        return new self(true, true, true, true, true, $requirePayment, false, 'customer');
    }

    /** A booking made by logged-in staff: walk-ins, off-grid starts, outside hours. Overlaps are still refused. */
    public static function admin(bool $complimentary = false): self
    {
        return new self(false, false, false, false, false, false, $complimentary, 'admin');
    }
}
