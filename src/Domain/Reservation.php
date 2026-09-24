<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class Reservation
{
    /** @param int[] $stationIds */
    public function __construct(
        public readonly int $id,
        public readonly string $uuid,
        public readonly string $confirmationCode,
        public readonly string $status,
        public readonly string $localDate,
        public readonly int $startMinute,
        public readonly int $durationMinutes,
        public readonly array $stationIds,
        public readonly string $startUtc,
        public readonly string $endUtc,
        public readonly int $subtotalCents,
        public readonly int $taxCents,
        public readonly int $totalCents,
        public readonly string $currency,
        public readonly ?string $holdExpiresAtUtc,
        public readonly ?string $paymentId,
    ) {
    }
}
