<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

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
