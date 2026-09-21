<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

/**
 * A booking the system refuses. $reason is a stable machine code:
 * validation_failed, date_in_past, too_far_ahead, closed, outside_hours, off_grid, too_soon,
 * duration_not_offered, invalid_station_count, slot_unavailable, hold_expired, not_found, wrong_status.
 */
final class BookingRejected extends \DomainException
{
    /** @param array<string,string> $fieldErrors */
    public function __construct(public readonly string $reason, public readonly array $fieldErrors = [])
    {
        parent::__construct($reason);
    }
}
