<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Support;

use OpenArcade\Support\Money;
use PHPUnit\Framework\TestCase;

final class MoneyTest extends TestCase
{
    public function testTimesAndPlus(): void
    {
        $price = Money::of(2500, 'USD');
        self::assertSame(7500, $price->times(3)->cents);
        self::assertSame(2600, $price->plus(Money::of(100, 'USD'))->cents);
    }

    public function testTaxRoundsHalfUpInBasisPoints(): void
    {
        // 9.35% of 50.00 = 4.675 -> 4.68
        self::assertSame(468, Money::of(5000, 'USD')->taxAt(935)->cents);
        // 9.35% of 25.00 = 2.3375 -> 2.34
        self::assertSame(234, Money::of(2500, 'USD')->taxAt(935)->cents);
        self::assertSame(0, Money::of(5000, 'USD')->taxAt(0)->cents);
    }

    public function testRejectsNegativeAmountsBadCurrencyAndMixedCurrencies(): void
    {
        foreach ([fn () => Money::of(-1, 'USD'), fn () => Money::of(1, 'usd'), fn () => Money::of(1, 'USD')->plus(Money::of(1, 'EUR'))] as $bad) {
            try {
                $bad();
                self::fail('expected InvalidArgumentException');
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }

    public function testFormat(): void
    {
        self::assertSame('USD 25.05', Money::of(2505, 'USD')->format());
    }
}
