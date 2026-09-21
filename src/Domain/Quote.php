<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

final class Quote
{
    public function __construct(
        public readonly int $subtotalCents,
        public readonly int $taxCents,
        public readonly int $totalCents,
        public readonly string $currency,
    ) {
    }
}
