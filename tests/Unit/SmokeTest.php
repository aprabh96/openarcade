<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit;

use PHPUnit\Framework\TestCase;

final class SmokeTest extends TestCase
{
    public function testPhpVersionIsSupported(): void
    {
        self::assertTrue(PHP_VERSION_ID >= 80100);
    }
}
