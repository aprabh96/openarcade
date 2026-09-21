<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Domain\PriceRepository;
use PHPUnit\Framework\TestCase;

final class PriceRepositoryTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testSetReplacesAndLoadBuildsPriceList(): void
    {
        $repo = new PriceRepository(TestDb::fresh());
        $repo->set(-1, 60, 2500);
        $repo->set(-1, 60, 2600);
        $repo->set(6, 60, 3000);
        $prices = $repo->load();
        self::assertSame(2600, $prices->priceCents(2, 60));
        self::assertSame(3000, $prices->priceCents(6, 60));
        $repo->remove(6, 60);
        self::assertSame(2600, $repo->load()->priceCents(6, 60));
    }

    public function testRejectsInvalidInput(): void
    {
        $repo = new PriceRepository(TestDb::fresh());
        foreach ([[7, 60, 100], [-2, 60, 100], [1, 0, 100], [1, 60, -1]] as [$weekday, $duration, $cents]) {
            try {
                $repo->set($weekday, $duration, $cents);
                self::fail('expected InvalidArgumentException');
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }
}
