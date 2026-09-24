<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Domain;

use OpenArcade\Domain\DayHours;
use PHPUnit\Framework\TestCase;

final class DayHoursTest extends TestCase
{
    public function testValidRange(): void
    {
        $hours = new DayHours(600, 1320);
        self::assertSame(600, $hours->openMinute);
        self::assertSame(1320, $hours->closeMinute);
    }

    public function testRejectsBadRanges(): void
    {
        foreach ([[600, 600], [700, 600], [-1, 600], [600, 1441]] as [$open, $close]) {
            try {
                new DayHours($open, $close);
                self::fail("expected exception for {$open}-{$close}");
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }
}
