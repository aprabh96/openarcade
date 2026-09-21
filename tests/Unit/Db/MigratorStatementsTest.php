<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Db;

use ArcadeOS\Db\Migrator;
use PHPUnit\Framework\TestCase;

final class MigratorStatementsTest extends TestCase
{
    public function testSplitsStatementsAndDropsCommentLines(): void
    {
        $sql = "-- header\r\nCREATE TABLE a (id INT);\n\n-- note\nCREATE TABLE b (\n  id INT\n);";
        self::assertSame(
            ['CREATE TABLE a (id INT)', "CREATE TABLE b (\n  id INT\n)"],
            Migrator::statements($sql)
        );
    }
}
