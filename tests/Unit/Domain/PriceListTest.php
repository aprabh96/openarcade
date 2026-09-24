<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Domain;

use OpenArcade\Domain\PriceList;
use PHPUnit\Framework\TestCase;

final class PriceListTest extends TestCase
{
    private function prices(): PriceList
    {
        return new PriceList([
            -1 => [60 => 2500, 90 => 3550],
            6 => [60 => 3000, 120 => 5600],
        ]);
    }

    public function testWeekdayOverrideThenDefaultThenNull(): void
    {
        self::assertSame(3000, $this->prices()->priceCents(6, 60));
        self::assertSame(3550, $this->prices()->priceCents(6, 90));
        self::assertSame(2500, $this->prices()->priceCents(1, 60));
        self::assertNull($this->prices()->priceCents(1, 120));
    }

    public function testDurationsAreTheSortedUnion(): void
    {
        self::assertSame([60, 90], $this->prices()->durations(1));
        self::assertSame([60, 90, 120], $this->prices()->durations(6));
    }

    public function testQuoteMultipliesByStationsAndAddsTax(): void
    {
        $quote = $this->prices()->quote(6, 60, 2, 935, 'USD');
        self::assertSame(6000, $quote->subtotalCents);
        self::assertSame(561, $quote->taxCents);
        self::assertSame(6561, $quote->totalCents);
        self::assertSame('USD', $quote->currency);
    }

    public function testQuoteRejectsDurationNotOffered(): void
    {
        $this->expectException(\InvalidArgumentException::class);
        $this->prices()->quote(1, 45, 1, 0, 'USD');
    }
}
