<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Tools;

use ArcadeOS\Tools\CleanScanner;
use PHPUnit\Framework\TestCase;

final class CleanScannerTest extends TestCase
{
    public function testFlagsSquareAccessToken(): void
    {
        $text = '$token = "' . 'EA' . 'AA' . str_repeat('x', 40) . '";';
        $findings = (new CleanScanner())->scanText('a.php', $text);
        self::assertSame(['square_access_token'], array_column($findings, 'rule'));
        self::assertSame(1, $findings[0]->line);
    }

    public function testFlagsRealLookingEmailButAllowsExampleDomains(): void
    {
        $scanner = new CleanScanner();
        $real = 'contact: jane' . '@' . 'somecompany.io';
        self::assertSame(['email'], array_column($scanner->scanText('a.md', $real), 'rule'));
        self::assertSame([], $scanner->scanText('a.md', 'contact: jane@example.com'));
    }

    public function testFlagsPhoneNumbersButAllowsFictional555Range(): void
    {
        $scanner = new CleanScanner();
        $real = 'call 785' . '-' . '842' . '-' . '9921';
        self::assertSame(['phone'], array_column($scanner->scanText('a.md', $real), 'rule'));
        self::assertSame([], $scanner->scanText('a.md', 'call 785-555-0142'));
    }

    public function testFlagsDenylistedTokenByHash(): void
    {
        $secret = 'Tr0ub4dor' . '-legacy-value';
        $scanner = new CleanScanner([hash('sha256', $secret)]);
        $findings = $scanner->scanText('config.php', "\$dbPassword = '" . $secret . "';");
        self::assertSame(['denylist'], array_column($findings, 'rule'));
        self::assertStringNotContainsString($secret, $findings[0]->preview);
    }

    public function testAllowlistSuppressesAnExactString(): void
    {
        $email = 'security' . '@' . 'somecompany.io';
        $scanner = new CleanScanner([], [$email]);
        self::assertSame([], $scanner->scanText('SECURITY.md', 'Report to ' . $email));
    }

    public function testCleanTextHasNoFindings(): void
    {
        $text = "<?php\n\$price = 2500; // cents\n\$date = '2026-09-21';\n";
        self::assertSame([], (new CleanScanner())->scanText('a.php', $text));
    }
}
