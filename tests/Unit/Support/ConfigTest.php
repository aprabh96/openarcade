<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Support;

use ArcadeOS\Support\Config;
use ArcadeOS\Support\Env;
use PHPUnit\Framework\TestCase;

final class ConfigTest extends TestCase
{
    public function testDefaultsAreProductionSafe(): void
    {
        $config = new Config(new Env([], false), '/app');
        self::assertSame('production', $config->appEnv());
        self::assertFalse($config->debug());
        self::assertSame('/app', $config->rootDir());
    }

    public function testAppKeyMustBeLongEnough(): void
    {
        $this->expectException(\RuntimeException::class);
        (new Config(new Env(['APP_KEY' => 'short'], false), '/app'))->appKey();
    }

    public function testDatabaseSettings(): void
    {
        $config = new Config(new Env([
            'DB_HOST' => 'db', 'DB_PORT' => '3307', 'DB_NAME' => 'n', 'DB_USER' => 'u', 'DB_PASSWORD' => 'p',
        ], false), '/app');
        self::assertSame(['host' => 'db', 'port' => 3307, 'name' => 'n', 'user' => 'u', 'password' => 'p'], $config->database());
    }
}
