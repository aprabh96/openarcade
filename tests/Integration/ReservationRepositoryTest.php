<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Domain\ReservationRepository;
use PHPUnit\Framework\TestCase;

final class ReservationRepositoryTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testInsertRejectsAnUnknownColumn(): void
    {
        $repository = new ReservationRepository(TestDb::connect());
        $this->expectException(\InvalidArgumentException::class);
        $repository->insert(['status' => 'confirmed', 'not_a_real_column' => 'x'], []);
    }
}
