<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Support;

use OpenArcade\Support\Env;
use PHPUnit\Framework\TestCase;

final class EnvTest extends TestCase
{
    public function testParsesKeysQuotesCommentsAndBlankLines(): void
    {
        $values = Env::parse("# comment\n\nAPP_ENV=production\nDB_NAME=\"my db\"\nAPP_KEY='abc#def'\nDB_PORT=3306 # inline\nnot a pair\nlower=ignored\n");
        self::assertSame(
            ['APP_ENV' => 'production', 'DB_NAME' => 'my db', 'APP_KEY' => 'abc#def', 'DB_PORT' => '3306'],
            $values
        );
    }

    public function testRealEnvironmentWinsOverFile(): void
    {
        putenv('OPENARCADE_TEST_KEY=from-real-env');
        try {
            $env = new Env(['OPENARCADE_TEST_KEY' => 'from-file', 'ONLY_IN_FILE' => 'x']);
            self::assertSame('from-real-env', $env->get('OPENARCADE_TEST_KEY'));
            self::assertSame('x', $env->get('ONLY_IN_FILE'));
            self::assertSame('fallback', $env->get('OPENARCADE_MISSING', 'fallback'));
        } finally {
            putenv('OPENARCADE_TEST_KEY');
        }
    }

    public function testTypedAccessors(): void
    {
        $env = new Env(['A_TRUE' => 'true', 'A_ONE' => '1', 'A_NO' => 'no', 'A_INT' => '42']);
        self::assertTrue($env->bool('A_TRUE', false));
        self::assertTrue($env->bool('A_ONE', false));
        self::assertFalse($env->bool('A_NO', true));
        self::assertTrue($env->bool('A_ABSENT', true));
        self::assertSame(42, $env->int('A_INT', 0));
        self::assertSame(7, $env->int('A_ABSENT', 7));
    }

    public function testRequireThrowsWhenMissing(): void
    {
        $this->expectException(\RuntimeException::class);
        $this->expectExceptionMessage('OPENARCADE_NOT_SET');
        (new Env([]))->require('OPENARCADE_NOT_SET');
    }

    public function testFromFileWithMissingFileIsEmpty(): void
    {
        self::assertNull(Env::fromFile('/nonexistent/.env')->get('OPENARCADE_ANYTHING'));
    }
}
