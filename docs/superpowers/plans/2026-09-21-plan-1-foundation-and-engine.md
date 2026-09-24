# Booking Core Plan 1: Foundation and Booking Engine

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A tested PHP library and CLI that can install a venue database and create reservations that can never double-book a station, with a gate that keeps secrets and personal data out of the repository.

**Architecture:** Plain PHP 8.1+ with PSR-4 classes under `src/`, PDO against MariaDB/MySQL, no framework. Pure domain logic (hours, pricing, availability, station allocation) is separated from persistence (repositories) and orchestrated by one `Reservations` service that serialises writers per date with a row lock. Everything runs and is tested inside Docker.

**Tech Stack:** PHP 8.2 (min 8.1), MariaDB 10.11, PDO, PHPUnit 10, PHPStan, PHP-CS-Fixer, Docker Compose, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-21-booking-core-design.md`. Later plans: 2 HTTP + auth + admin API, 3 payments + real-time + mail, 4 booking UI + dashboard UI, 5 doctor + setup page + agent kit + docs.

**Rules for every task**
- Never copy code, data, names, emails, phone numbers, keys or identifiers from the legacy VR Lawrence folders. This repository is new code.
- Test data uses only `@example.com` emails and `555-01xx` phone numbers.
- Every PHP file starts with `<?php` then `declare(strict_types=1);`.
- Run commands from the repository root `E:\openarcade`. All PHP runs in Docker: `docker compose run --rm app <command>`.
- Commit after each task with the message shown. End every commit message with the trailer `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

---

## File map (created by this plan)

| Path | Responsibility |
| --- | --- |
| `composer.json`, `phpunit.xml`, `phpstan.neon`, `.php-cs-fixer.php`, `.gitattributes`, `.editorconfig`, `.env.example` | Tooling and config templates |
| `Dockerfile`, `docker-compose.yml`, `docker/db-init/01-test-db.sql` | Local runtime and test database |
| `LICENSE`, `README.md` | MIT license, short readme (expanded in Plan 5) |
| `tools/CleanScanner.php`, `bin/check-clean`, `tools/clean-allowlist.txt` | Secret and personal-data gate |
| `src/Support/Env.php`, `Config.php`, `Clock.php`, `SystemClock.php`, `FixedClock.php`, `Money.php` | Environment, config, time, money |
| `src/Db/Connection.php`, `Migrator.php`, `Transaction.php` | PDO factory, migrations, retrying transactions |
| `migrations/001_init.sql` | Schema |
| `src/Settings/VenueSettings.php`, `SettingsRepository.php` | Venue settings |
| `src/Domain/DayHours.php`, `HoursRepository.php` | Opening hours |
| `src/Domain/Quote.php`, `PriceList.php`, `PriceRepository.php` | Pricing |
| `src/Domain/Block.php`, `Slot.php`, `Availability.php`, `StationAllocator.php` | Availability engine |
| `src/Domain/StationRepository.php`, `BookingRequest.php`, `BookingRules.php`, `BookingRejected.php`, `Reservation.php`, `ReservationRepository.php`, `Reservations.php` | Reservation write path |
| `src/Console/Application.php`, `bin/console` | CLI: migrate, install, admin:create, seed:demo |
| `tests/Unit/**`, `tests/Integration/**` | Tests |
| `.github/workflows/ci.yml` | CI |

---

### Task 1: Project scaffold

**Files:** Create `composer.json`, `phpunit.xml`, `phpstan.neon`, `.php-cs-fixer.php`, `.gitattributes`, `.editorconfig`, `.env.example`, `Dockerfile`, `docker-compose.yml`, `docker/db-init/01-test-db.sql`, `LICENSE`, `README.md`, `storage/logs/.gitkeep`, `storage/cache/.gitkeep`, `tests/Unit/SmokeTest.php`. Modify `.gitignore`.

- [ ] **Step 1: Write `composer.json`**

```json
{
  "name": "psynect/openarcade",
  "description": "Self-hosted reservation system for VR arcades and other station-based venues.",
  "type": "project",
  "license": "MIT",
  "require": {
    "php": ">=8.1",
    "ext-json": "*",
    "ext-mbstring": "*",
    "ext-pdo": "*",
    "ext-pdo_mysql": "*"
  },
  "require-dev": {
    "friendsofphp/php-cs-fixer": "^3.59",
    "phpstan/phpstan": "^1.11",
    "phpunit/phpunit": "^10.5"
  },
  "autoload": { "psr-4": { "OpenArcade\\": "src/" } },
  "autoload-dev": {
    "psr-4": { "OpenArcade\\Tests\\": "tests/", "OpenArcade\\Tools\\": "tools/" }
  },
  "scripts": {
    "test": "@php vendor/bin/phpunit",
    "test:unit": "@php vendor/bin/phpunit --testsuite unit",
    "test:integration": "@php vendor/bin/phpunit --testsuite integration",
    "stan": "@php vendor/bin/phpstan analyse --no-progress --memory-limit=512M",
    "cs": "@php vendor/bin/php-cs-fixer fix --dry-run --diff",
    "cs:fix": "@php vendor/bin/php-cs-fixer fix",
    "clean-check": "@php bin/check-clean",
    "check": ["@cs", "@stan", "@test", "@clean-check"]
  },
  "config": { "sort-packages": true }
}
```

- [ ] **Step 2: Write `phpunit.xml`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<phpunit xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
         xsi:noNamespaceSchemaLocation="vendor/phpunit/phpunit/phpunit.xsd"
         bootstrap="vendor/autoload.php"
         colors="true"
         cacheDirectory=".phpunit.cache"
         failOnWarning="true"
         failOnRisky="true">
  <testsuites>
    <testsuite name="unit"><directory>tests/Unit</directory></testsuite>
    <testsuite name="integration"><directory>tests/Integration</directory></testsuite>
  </testsuites>
  <source><include><directory>src</directory></include></source>
</phpunit>
```

- [ ] **Step 3: Write `phpstan.neon` and `.php-cs-fixer.php`**

`phpstan.neon`:
```neon
parameters:
  level: 6
  paths:
    - src
    - tools
  tmpDir: .phpstan.cache
```

`.php-cs-fixer.php`:
```php
<?php

$finder = PhpCsFixer\Finder::create()->in([__DIR__ . '/src', __DIR__ . '/tests', __DIR__ . '/tools']);

return (new PhpCsFixer\Config())
    ->setRiskyAllowed(true)
    ->setRules([
        '@PSR12' => true,
        'declare_strict_types' => true,
        'no_unused_imports' => true,
        'ordered_imports' => true,
        'single_quote' => true,
    ])
    ->setFinder($finder);
```

- [ ] **Step 4: Write `.gitattributes`, `.editorconfig`, and replace `.gitignore`**

`.gitattributes`:
```
* text=auto eol=lf
*.png binary
*.jpg binary
*.ico binary
```

`.editorconfig`:
```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
indent_style = space
indent_size = 4
trim_trailing_whitespace = true

[*.{yml,yaml,json,md}]
indent_size = 2
```

`.gitignore` (full replacement):
```
.env
/vendor/
/storage/logs/*
/storage/cache/*
!/storage/logs/.gitkeep
!/storage/cache/.gitkeep
/.phpunit.cache/
/.phpstan.cache/
/.php-cs-fixer.cache
/node_modules/
/tools/denylist.local.sha256
.DS_Store
Thumbs.db
*.log
```

- [ ] **Step 5: Write `.env.example`**

```dotenv
# Copy to .env and fill in. Never commit .env.
APP_ENV=production
APP_DEBUG=false
APP_URL=https://booking.example.com
# 32+ random characters. Generate: php -r "echo bin2hex(random_bytes(32));"
APP_KEY=

DB_HOST=localhost
DB_PORT=3306
DB_NAME=
DB_USER=
DB_PASSWORD=
```

- [ ] **Step 6: Write `Dockerfile`, `docker-compose.yml`, `docker/db-init/01-test-db.sql`**

`Dockerfile`:
```dockerfile
FROM php:8.2-apache
RUN apt-get update && apt-get install -y --no-install-recommends git unzip \
 && docker-php-ext-install pdo_mysql \
 && a2enmod rewrite headers \
 && rm -rf /var/lib/apt/lists/*
COPY --from=composer:2 /usr/bin/composer /usr/bin/composer
ENV APACHE_DOCUMENT_ROOT=/var/www/app/public
RUN sed -ri 's!/var/www/html!${APACHE_DOCUMENT_ROOT}!g' /etc/apache2/sites-available/*.conf /etc/apache2/apache2.conf /etc/apache2/conf-available/*.conf \
 && printf '<Directory /var/www/app/public>\n    AllowOverride All\n    Require all granted\n</Directory>\n' > /etc/apache2/conf-available/app.conf \
 && a2enconf app
WORKDIR /var/www/app
```

`docker-compose.yml` (development only; these credentials are local throwaways):
```yaml
services:
  app:
    build: .
    ports:
      - "8088:80"
    volumes:
      - .:/var/www/app
    environment:
      APP_ENV: local
      APP_DEBUG: "true"
      APP_URL: http://localhost:8088
      APP_KEY: local-development-key-not-for-production-0000
      DB_HOST: db
      DB_PORT: "3306"
      DB_NAME: openarcade
      DB_USER: openarcade
      DB_PASSWORD: openarcade
      TEST_DB_NAME: openarcade_test
    depends_on:
      db:
        condition: service_healthy
  db:
    image: mariadb:10.11
    environment:
      MARIADB_ROOT_PASSWORD: root
      MARIADB_DATABASE: openarcade
      MARIADB_USER: openarcade
      MARIADB_PASSWORD: openarcade
    volumes:
      - dbdata:/var/lib/mysql
      - ./docker/db-init:/docker-entrypoint-initdb.d:ro
    healthcheck:
      test: ["CMD", "healthcheck.sh", "--connect", "--innodb_initialized"]
      interval: 5s
      timeout: 5s
      retries: 20
volumes:
  dbdata: {}
```

`docker/db-init/01-test-db.sql`:
```sql
CREATE DATABASE IF NOT EXISTS openarcade_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
GRANT ALL PRIVILEGES ON openarcade_test.* TO 'openarcade'@'%';
FLUSH PRIVILEGES;
```

- [ ] **Step 7: Write `LICENSE` (MIT, `Copyright (c) 2026 Prabhsimran Arora`), a short `README.md`, the two `.gitkeep` files, and a smoke test**

`README.md`:
```markdown
# openarcade (working name)

Self-hosted reservation system for VR arcades and other venues that rent numbered stations by the hour. Work in progress. See `docs/superpowers/specs/` for the design.

## Development

    docker compose build
    docker compose run --rm app composer install
    docker compose run --rm app composer check
```

`tests/Unit/SmokeTest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit;

use PHPUnit\Framework\TestCase;

final class SmokeTest extends TestCase
{
    public function testPhpVersionIsSupported(): void
    {
        self::assertTrue(PHP_VERSION_ID >= 80100);
    }
}
```

- [ ] **Step 8: Build and run**

Run: `docker compose build app` then `docker compose run --rm app composer install` then `docker compose run --rm app composer test:unit`
Expected: build succeeds; composer installs; PHPUnit prints `OK (1 test, 1 assertion)`.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "Scaffold project: composer, docker, tooling"
```

---

### Task 2: Clean-repository gate

Blocks secrets and personal data from ever being committed. `tools/denylist.local.sha256` is optional, git-ignored, and holds SHA-256 hashes (one per line) of strings that must never appear; the maintainer generates it locally.

**Files:** Create `tools/Finding.php`, `tools/CleanScanner.php`, `tools/clean-allowlist.txt`, `bin/check-clean`, `tests/Unit/Tools/CleanScannerTest.php`.

- [ ] **Step 1: Write the failing test `tests/Unit/Tools/CleanScannerTest.php`**

Fixtures are assembled with concatenation so this test file never contains a string that the scanner itself would flag.

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Tools;

use OpenArcade\Tools\CleanScanner;
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
```

- [ ] **Step 2: Run it to see it fail**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Tools/CleanScannerTest.php`
Expected: FAIL, `Class "OpenArcade\Tools\CleanScanner" not found`.

- [ ] **Step 3: Write `tools/Finding.php` and `tools/CleanScanner.php`**

`tools/Finding.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tools;

final class Finding
{
    public function __construct(
        public readonly string $file,
        public readonly int $line,
        public readonly string $rule,
        public readonly string $preview,
    ) {
    }
}
```

`tools/CleanScanner.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tools;

final class CleanScanner
{
    private const KEY_RULES = [
        'square_access_token' => '/\bEAAA[A-Za-z0-9_\-]{20,}/',
        'square_application_id' => '/\b(?:sandbox-)?sq0(?:idp|csp|atp)-[A-Za-z0-9_\-]{10,}/',
        'google_api_key' => '/\bAIza[0-9A-Za-z_\-]{30,}/',
        'openai_style_key' => '/\bsk-[A-Za-z0-9_\-]{20,}/',
        'stripe_key' => '/\b[sp]k_(?:live|test)_[A-Za-z0-9]{16,}/',
        'private_key_block' => '/-----BEGIN [A-Z ]*PRIVATE KEY-----/',
        'jwt' => '/\beyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}/',
    ];
    private const EMAIL = '/[A-Za-z0-9._%+\-]+@([A-Za-z0-9.\-]+\.[A-Za-z]{2,})/';
    private const PHONE = '/(?<![\d.\-])(?:\+?1[\s\-.])?\(?\d{3}\)?[\s\-.]\d{3}[\s\-.]\d{4}(?![\d\-])/';
    private const FICTIONAL_PHONE = '/555[\s\-.]01\d\d/';
    private const ALLOWED_EMAIL_DOMAINS = ['example.com', 'example.org', 'example.net', 'arcade.test'];
    private const TOKEN = '/[A-Za-z0-9_\-\.\+\/=\^%\$#@!~\*]{8,}/';

    /**
     * @param string[] $denylistHashes lower-case SHA-256 hex digests of forbidden strings
     * @param string[] $allowlist exact strings that are never reported
     */
    public function __construct(private array $denylistHashes = [], private array $allowlist = [])
    {
        $this->denylistHashes = array_map('strtolower', $this->denylistHashes);
    }

    /** @return Finding[] */
    public function scanText(string $file, string $text): array
    {
        $findings = [];
        $lines = preg_split('/\R/', $text) ?: [];
        foreach ($lines as $index => $line) {
            $lineNo = $index + 1;
            foreach (self::KEY_RULES as $rule => $pattern) {
                if (preg_match_all($pattern, $line, $m)) {
                    foreach ($m[0] as $match) {
                        $this->add($findings, $file, $lineNo, $rule, $match);
                    }
                }
            }
            if (preg_match_all(self::EMAIL, $line, $m, PREG_SET_ORDER)) {
                foreach ($m as $match) {
                    if (!in_array(strtolower($match[1]), self::ALLOWED_EMAIL_DOMAINS, true)) {
                        $this->add($findings, $file, $lineNo, 'email', $match[0]);
                    }
                }
            }
            if (preg_match_all(self::PHONE, $line, $m)) {
                foreach ($m[0] as $match) {
                    if (!preg_match(self::FICTIONAL_PHONE, $match)) {
                        $this->add($findings, $file, $lineNo, 'phone', $match);
                    }
                }
            }
            if ($this->denylistHashes !== [] && preg_match_all(self::TOKEN, $line, $m)) {
                foreach ($m[0] as $token) {
                    foreach ([$token, trim($token, '.,;:!')] as $candidate) {
                        if (in_array(hash('sha256', $candidate), $this->denylistHashes, true)) {
                            $this->add($findings, $file, $lineNo, 'denylist', $candidate);
                            break;
                        }
                    }
                }
            }
        }

        return $findings;
    }

    /** @param Finding[] $findings */
    private function add(array &$findings, string $file, int $line, string $rule, string $match): void
    {
        if (in_array($match, $this->allowlist, true)) {
            return;
        }
        $length = strlen($match);
        $preview = $length <= 6
            ? str_repeat('*', $length)
            : substr($match, 0, 2) . str_repeat('*', min(12, $length - 4)) . substr($match, -2) . " (len {$length})";

        $findings[] = new Finding($file, $line, $rule, $preview);
    }
}
```

- [ ] **Step 4: Run the test**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Tools/CleanScannerTest.php`
Expected: `OK (6 tests, ...)`.

- [ ] **Step 5: Write `tools/clean-allowlist.txt` (one exact string per line, `#` comments) and `bin/check-clean`**

`tools/clean-allowlist.txt`:
```
# Exact strings the clean check may ignore. Keep this list short and explain every entry.
# Commit trailer quoted in docs/superpowers/plans.
noreply@anthropic.com
```

`bin/check-clean`:
```php
#!/usr/bin/env php
<?php

declare(strict_types=1);

require __DIR__ . '/../vendor/autoload.php';

use OpenArcade\Tools\CleanScanner;

$root = dirname(__DIR__);
chdir($root);

$readList = static function (string $path): array {
    if (!is_file($path)) {
        return [];
    }
    $lines = array_map('trim', file($path, FILE_IGNORE_NEW_LINES) ?: []);

    return array_values(array_filter($lines, static fn (string $l): bool => $l !== '' && $l[0] !== '#'));
};

$scanner = new CleanScanner(
    $readList($root . '/tools/denylist.local.sha256'),
    $readList($root . '/tools/clean-allowlist.txt'),
);

$files = [];
exec('git ls-files --cached --others --exclude-standard', $files, $code);
if ($code !== 0) {
    fwrite(STDERR, "check-clean: git ls-files failed\n");
    exit(2);
}

$skipNames = ['composer.lock', 'package-lock.json'];
$skipExtensions = ['png', 'jpg', 'jpeg', 'gif', 'ico', 'webp', 'woff', 'woff2', 'pdf', 'zip'];
$total = 0;
foreach ($files as $file) {
    if (in_array(basename($file), $skipNames, true)) {
        continue;
    }
    if (in_array(strtolower(pathinfo($file, PATHINFO_EXTENSION)), $skipExtensions, true)) {
        continue;
    }
    if (!is_file($file) || filesize($file) > 2_000_000) {
        continue;
    }
    foreach ($scanner->scanText($file, (string) file_get_contents($file)) as $finding) {
        $total++;
        fwrite(STDERR, sprintf("%s:%d [%s] %s\n", $finding->file, $finding->line, $finding->rule, $finding->preview));
    }
}

if ($total > 0) {
    fwrite(STDERR, "check-clean: {$total} problem(s). Remove them, or add an exact string to tools/clean-allowlist.txt with a reason.\n");
    exit(1);
}
echo "check-clean: OK (" . count($files) . " files)\n";
```

- [ ] **Step 6: Run the gate on the repository**

Run: `docker compose run --rm app composer clean-check`
Expected: `check-clean: OK (N files)`. If it reports a finding in a file you wrote, fix the file; do not allowlist test fixtures.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Add clean-repository gate for secrets and personal data"
```

---

### Task 3: Env and Config

**Files:** Create `src/Support/Env.php`, `src/Support/Config.php`, `tests/Unit/Support/EnvTest.php`, `tests/Unit/Support/ConfigTest.php`.

- [ ] **Step 1: Write the failing tests**

`tests/Unit/Support/EnvTest.php`:
```php
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
```

`tests/Unit/Support/ConfigTest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Support;

use OpenArcade\Support\Config;
use OpenArcade\Support\Env;
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
```

`ConfigTest` builds `Env` with its second argument `false` so the real environment (the compose file sets `APP_ENV` and `APP_DEBUG`) cannot leak into the assertions.

- [ ] **Step 2: Run to see them fail**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Support`
Expected: FAIL, classes not found.

- [ ] **Step 3: Write `src/Support/Env.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Support;

final class Env
{
    /** @param array<string,string> $fileValues */
    public function __construct(private array $fileValues = [], private bool $useRealEnvironment = true)
    {
    }

    public static function fromFile(?string $path): self
    {
        if ($path === null || !is_file($path) || !is_readable($path)) {
            return new self([]);
        }

        return new self(self::parse((string) file_get_contents($path)));
    }

    /** @return array<string,string> */
    public static function parse(string $contents): array
    {
        $values = [];
        foreach (preg_split('/\R/', $contents) ?: [] as $line) {
            $line = trim($line);
            if ($line === '' || $line[0] === '#') {
                continue;
            }
            $pos = strpos($line, '=');
            if ($pos === false) {
                continue;
            }
            $key = trim(substr($line, 0, $pos));
            if (preg_match('/^[A-Z][A-Z0-9_]*$/', $key) !== 1) {
                continue;
            }
            $value = trim(substr($line, $pos + 1));
            $length = strlen($value);
            $quoted = $length >= 2
                && (($value[0] === '"' && $value[$length - 1] === '"') || ($value[0] === "'" && $value[$length - 1] === "'"));
            if ($quoted) {
                $value = substr($value, 1, -1);
            } else {
                $hash = strpos($value, ' #');
                if ($hash !== false) {
                    $value = rtrim(substr($value, 0, $hash));
                }
            }
            $values[$key] = $value;
        }

        return $values;
    }

    public function get(string $key, ?string $default = null): ?string
    {
        if ($this->useRealEnvironment) {
            $real = getenv($key);
            if ($real !== false && $real !== '') {
                return $real;
            }
        }
        if (isset($this->fileValues[$key]) && $this->fileValues[$key] !== '') {
            return $this->fileValues[$key];
        }

        return $default;
    }

    public function require(string $key): string
    {
        $value = $this->get($key);
        if ($value === null) {
            throw new \RuntimeException("Missing required setting {$key}. Add it to .env.");
        }

        return $value;
    }

    public function bool(string $key, bool $default): bool
    {
        $value = $this->get($key);
        if ($value === null) {
            return $default;
        }

        return in_array(strtolower($value), ['1', 'true', 'yes', 'on'], true);
    }

    public function int(string $key, int $default): int
    {
        $value = $this->get($key);

        return $value === null || !is_numeric($value) ? $default : (int) $value;
    }
}
```

- [ ] **Step 4: Write `src/Support/Config.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Support;

final class Config
{
    public function __construct(private Env $env, private string $rootDir)
    {
    }

    public static function load(string $rootDir): self
    {
        return new self(Env::fromFile($rootDir . '/.env'), $rootDir);
    }

    public function env(): Env
    {
        return $this->env;
    }

    public function rootDir(): string
    {
        return $this->rootDir;
    }

    public function appEnv(): string
    {
        return $this->env->get('APP_ENV', 'production') ?? 'production';
    }

    public function debug(): bool
    {
        return $this->env->bool('APP_DEBUG', false);
    }

    public function appKey(): string
    {
        $key = $this->env->require('APP_KEY');
        if (strlen($key) < 32) {
            throw new \RuntimeException('APP_KEY must be at least 32 characters.');
        }

        return $key;
    }

    /** @return array{host:string,port:int,name:string,user:string,password:string} */
    public function database(): array
    {
        return [
            'host' => $this->env->get('DB_HOST', 'localhost') ?? 'localhost',
            'port' => $this->env->int('DB_PORT', 3306),
            'name' => $this->env->require('DB_NAME'),
            'user' => $this->env->require('DB_USER'),
            'password' => $this->env->get('DB_PASSWORD', '') ?? '',
        ];
    }
}
```

- [ ] **Step 5: Run the tests**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Support`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Add Env parser and Config"
```

---

### Task 4: Clock and Money

**Files:** Create `src/Support/Clock.php`, `SystemClock.php`, `FixedClock.php`, `Money.php`, `tests/Unit/Support/MoneyTest.php`, `tests/Unit/Support/ClockTest.php`.

- [ ] **Step 1: Write the failing tests**

`tests/Unit/Support/MoneyTest.php`:
```php
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
```

`tests/Unit/Support/ClockTest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Support;

use OpenArcade\Support\FixedClock;
use OpenArcade\Support\SystemClock;
use PHPUnit\Framework\TestCase;

final class ClockTest extends TestCase
{
    public function testFixedClockReturnsUtcInstantAndCanAdvance(): void
    {
        $clock = new FixedClock('2026-03-08 15:00:00');
        self::assertSame('2026-03-08 15:00:00 UTC', $clock->now()->format('Y-m-d H:i:s T'));
        $clock->advanceMinutes(90);
        self::assertSame('2026-03-08 16:30:00', $clock->now()->format('Y-m-d H:i:s'));
    }

    public function testSystemClockIsUtc(): void
    {
        self::assertSame('UTC', (new SystemClock())->now()->getTimezone()->getName());
    }
}
```

- [ ] **Step 2: Run to see them fail**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Support`
Expected: FAIL, classes not found.

- [ ] **Step 3: Write the four classes**

`src/Support/Clock.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Support;

interface Clock
{
    /** Current instant, always in UTC. */
    public function now(): \DateTimeImmutable;
}
```

`src/Support/SystemClock.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Support;

final class SystemClock implements Clock
{
    public function now(): \DateTimeImmutable
    {
        return new \DateTimeImmutable('now', new \DateTimeZone('UTC'));
    }
}
```

`src/Support/FixedClock.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Support;

final class FixedClock implements Clock
{
    private \DateTimeImmutable $now;

    public function __construct(string $utcDateTime)
    {
        $this->now = new \DateTimeImmutable($utcDateTime, new \DateTimeZone('UTC'));
    }

    public function now(): \DateTimeImmutable
    {
        return $this->now;
    }

    public function advanceMinutes(int $minutes): void
    {
        $this->now = $this->now->modify(sprintf('%+d minutes', $minutes));
    }
}
```

`src/Support/Money.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Support;

final class Money
{
    private function __construct(public readonly int $cents, public readonly string $currency)
    {
    }

    public static function of(int $cents, string $currency): self
    {
        if ($cents < 0) {
            throw new \InvalidArgumentException('Money cannot be negative.');
        }
        if (preg_match('/^[A-Z]{3}$/', $currency) !== 1) {
            throw new \InvalidArgumentException('Currency must be a 3-letter upper-case code.');
        }

        return new self($cents, $currency);
    }

    public function times(int $quantity): self
    {
        return self::of($this->cents * $quantity, $this->currency);
    }

    public function plus(self $other): self
    {
        if ($other->currency !== $this->currency) {
            throw new \InvalidArgumentException('Cannot add different currencies.');
        }

        return self::of($this->cents + $other->cents, $this->currency);
    }

    /** Tax at a rate in basis points (935 = 9.35%), rounded half up to the cent. */
    public function taxAt(int $basisPoints): self
    {
        if ($basisPoints < 0) {
            throw new \InvalidArgumentException('Tax rate cannot be negative.');
        }

        return self::of(intdiv($this->cents * $basisPoints + 5000, 10000), $this->currency);
    }

    public function format(): string
    {
        return sprintf('%s %d.%02d', $this->currency, intdiv($this->cents, 100), $this->cents % 100);
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Support`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Add Clock and Money value types"
```

---

### Task 5: Database connection, migrator, schema

**Files:** Create `src/Db/Connection.php`, `src/Db/Migrator.php`, `src/Db/Transaction.php`, `migrations/001_init.sql`, `tests/Unit/Db/MigratorStatementsTest.php`, `tests/Integration/TestDb.php`, `tests/Integration/MigrationTest.php`.

- [ ] **Step 1: Write the failing unit test `tests/Unit/Db/MigratorStatementsTest.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Db;

use OpenArcade\Db\Migrator;
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
```

- [ ] **Step 2: Write `src/Db/Connection.php`, `src/Db/Migrator.php`, `src/Db/Transaction.php`**

`src/Db/Connection.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Db;

use OpenArcade\Support\Config;
use PDO;

final class Connection
{
    public static function make(string $host, int $port, string $database, string $user, string $password): PDO
    {
        $pdo = new PDO(
            "mysql:host={$host};port={$port};dbname={$database};charset=utf8mb4",
            $user,
            $password,
            [
                PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
                PDO::ATTR_EMULATE_PREPARES => false,
            ]
        );
        // Every DATETIME in this application is UTC.
        $pdo->exec("SET time_zone = '+00:00'");

        return $pdo;
    }

    public static function fromConfig(Config $config): PDO
    {
        $db = $config->database();

        return self::make($db['host'], $db['port'], $db['name'], $db['user'], $db['password']);
    }
}
```

`src/Db/Migrator.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Db;

use PDO;

final class Migrator
{
    public function __construct(private PDO $pdo, private string $directory)
    {
    }

    /** @return string[] versions applied by this call */
    public function migrate(): array
    {
        $this->pdo->exec(
            'CREATE TABLE IF NOT EXISTS migrations ('
            . 'version VARCHAR(100) NOT NULL PRIMARY KEY, applied_at DATETIME NOT NULL'
            . ') ENGINE=InnoDB DEFAULT CHARSET=utf8mb4'
        );
        $query = $this->pdo->query('SELECT version FROM migrations');
        $done = $query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN);

        $files = glob($this->directory . '/*.sql') ?: [];
        sort($files);
        $applied = [];
        foreach ($files as $file) {
            $version = basename($file, '.sql');
            if (in_array($version, $done, true)) {
                continue;
            }
            foreach (self::statements((string) file_get_contents($file)) as $statement) {
                $this->pdo->exec($statement);
            }
            $this->pdo->prepare('INSERT INTO migrations (version, applied_at) VALUES (?, UTC_TIMESTAMP())')
                ->execute([$version]);
            $applied[] = $version;
        }

        return $applied;
    }

    /**
     * Migration files contain plain statements ending in ";" at end of line, and "--" comment lines.
     *
     * @return string[]
     */
    public static function statements(string $sql): array
    {
        $sql = str_replace("\r\n", "\n", $sql);
        $sql = preg_replace('/^\s*--.*$/m', '', $sql) ?? $sql;
        $parts = array_map('trim', explode(";\n", $sql . "\n"));

        return array_values(array_filter($parts, static fn (string $part): bool => $part !== ''));
    }
}
```

`src/Db/Transaction.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Db;

use PDO;

final class Transaction
{
    /**
     * Runs $work in a transaction and retries when MySQL reports a deadlock (1213) or lock wait timeout (1205).
     *
     * @template T
     * @param callable(PDO): T $work
     * @return T
     */
    public static function run(PDO $pdo, callable $work, int $maxAttempts = 3): mixed
    {
        $attempt = 0;
        while (true) {
            $attempt++;
            $pdo->beginTransaction();
            try {
                $result = $work($pdo);
                $pdo->commit();

                return $result;
            } catch (\Throwable $error) {
                if ($pdo->inTransaction()) {
                    $pdo->rollBack();
                }
                $retryable = $error instanceof \PDOException
                    && in_array((int) ($error->errorInfo[1] ?? 0), [1205, 1213], true);
                if ($retryable && $attempt < $maxAttempts) {
                    usleep(random_int(10_000, 60_000));
                    continue;
                }
                throw $error;
            }
        }
    }
}
```

- [ ] **Step 3: Write `migrations/001_init.sql`**

Times of day are stored as minutes after local midnight (0-1440). `prices.weekday` uses -1 for "every day" so a UNIQUE key can enforce one price per duration.

```sql
-- Booking Core schema, version 1. All DATETIME values are UTC.

CREATE TABLE settings (
  `key` VARCHAR(64) NOT NULL PRIMARY KEY,
  `value` TEXT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE stations (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  number INT UNSIGNED NOT NULL,
  label VARCHAR(60) NOT NULL,
  active TINYINT(1) NOT NULL DEFAULT 1,
  UNIQUE KEY uq_stations_number (number)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE business_hours (
  weekday TINYINT UNSIGNED NOT NULL PRIMARY KEY COMMENT '0 = Sunday .. 6 = Saturday',
  open_minute SMALLINT UNSIGNED NOT NULL,
  close_minute SMALLINT UNSIGNED NOT NULL,
  closed TINYINT(1) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE special_hours (
  local_date DATE NOT NULL PRIMARY KEY,
  open_minute SMALLINT UNSIGNED NOT NULL,
  close_minute SMALLINT UNSIGNED NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE closed_dates (
  local_date DATE NOT NULL PRIMARY KEY,
  reason VARCHAR(120) NOT NULL DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE prices (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  weekday TINYINT NOT NULL DEFAULT -1 COMMENT '-1 = every day, 0 = Sunday .. 6 = Saturday',
  duration_minutes INT UNSIGNED NOT NULL,
  price_cents INT UNSIGNED NOT NULL,
  UNIQUE KEY uq_prices_weekday_duration (weekday, duration_minutes)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE booking_days (
  local_date DATE NOT NULL PRIMARY KEY
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE reservations (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  uuid CHAR(36) NOT NULL,
  confirmation_code VARCHAR(12) NOT NULL,
  status VARCHAR(20) NOT NULL,
  first_name VARCHAR(60) NOT NULL,
  last_name VARCHAR(60) NOT NULL,
  email VARCHAR(190) NOT NULL,
  phone VARCHAR(30) NOT NULL,
  comments TEXT NULL,
  local_date DATE NOT NULL,
  start_utc DATETIME NOT NULL,
  end_utc DATETIME NOT NULL,
  duration_minutes INT UNSIGNED NOT NULL,
  station_count INT UNSIGNED NOT NULL,
  subtotal_cents INT UNSIGNED NOT NULL,
  tax_cents INT UNSIGNED NOT NULL,
  total_cents INT UNSIGNED NOT NULL,
  currency CHAR(3) NOT NULL,
  payment_provider VARCHAR(20) NOT NULL DEFAULT 'none',
  payment_id VARCHAR(191) NULL,
  hold_expires_at DATETIME NULL,
  timer_status VARCHAR(20) NOT NULL DEFAULT 'not_started',
  timer_end_utc DATETIME NULL,
  created_by VARCHAR(20) NOT NULL DEFAULT 'customer',
  created_at DATETIME NOT NULL,
  updated_at DATETIME NOT NULL,
  UNIQUE KEY uq_reservations_uuid (uuid),
  UNIQUE KEY uq_reservations_code (confirmation_code),
  KEY ix_reservations_date_status (local_date, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE reservation_stations (
  reservation_id INT UNSIGNED NOT NULL,
  station_id INT UNSIGNED NOT NULL,
  PRIMARY KEY (reservation_id, station_id),
  KEY ix_reservation_stations_station (station_id),
  CONSTRAINT fk_rs_reservation FOREIGN KEY (reservation_id) REFERENCES reservations (id) ON DELETE CASCADE,
  CONSTRAINT fk_rs_station FOREIGN KEY (station_id) REFERENCES stations (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE admins (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(50) NOT NULL,
  password_hash VARCHAR(255) NOT NULL,
  created_at DATETIME NOT NULL,
  last_login_at DATETIME NULL,
  UNIQUE KEY uq_admins_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE login_attempts (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(50) NOT NULL,
  ip_hash CHAR(64) NOT NULL,
  succeeded TINYINT(1) NOT NULL,
  created_at DATETIME NOT NULL,
  KEY ix_login_attempts_lookup (username, ip_hash, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE rate_limits (
  bucket VARCHAR(40) NOT NULL,
  ip_hash CHAR(64) NOT NULL,
  window_start DATETIME NOT NULL,
  hits INT UNSIGNED NOT NULL,
  PRIMARY KEY (bucket, ip_hash, window_start)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

- [ ] **Step 4: Write the integration helper `tests/Integration/TestDb.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Db\Connection;
use OpenArcade\Db\Migrator;
use PDO;

final class TestDb
{
    public static function available(): bool
    {
        return getenv('DB_HOST') !== false && getenv('TEST_DB_NAME') !== false;
    }

    public static function connect(): PDO
    {
        return Connection::make(
            (string) getenv('DB_HOST'),
            (int) (getenv('DB_PORT') ?: 3306),
            (string) getenv('TEST_DB_NAME'),
            (string) getenv('DB_USER'),
            (string) getenv('DB_PASSWORD'),
        );
    }

    /** Drops every table and re-applies all migrations. */
    public static function fresh(): PDO
    {
        $pdo = self::connect();
        $pdo->exec('SET FOREIGN_KEY_CHECKS = 0');
        $query = $pdo->query('SHOW TABLES');
        foreach ($query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN) as $table) {
            $pdo->exec('DROP TABLE `' . str_replace('`', '', (string) $table) . '`');
        }
        $pdo->exec('SET FOREIGN_KEY_CHECKS = 1');
        (new Migrator($pdo, dirname(__DIR__, 2) . '/migrations'))->migrate();

        return $pdo;
    }
}
```

- [ ] **Step 5: Write `tests/Integration/MigrationTest.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Db\Migrator;
use PDO;
use PHPUnit\Framework\TestCase;

final class MigrationTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testMigrationsCreateEveryTableAndAreIdempotent(): void
    {
        $pdo = TestDb::fresh();
        $query = $pdo->query('SHOW TABLES');
        $tables = $query === false ? [] : $query->fetchAll(PDO::FETCH_COLUMN);
        foreach (['settings', 'stations', 'business_hours', 'special_hours', 'closed_dates', 'prices', 'booking_days', 'reservations', 'reservation_stations', 'admins', 'login_attempts', 'rate_limits', 'migrations'] as $expected) {
            self::assertContains($expected, $tables);
        }
        $again = (new Migrator($pdo, dirname(__DIR__, 2) . '/migrations'))->migrate();
        self::assertSame([], $again);
    }

    public function testConnectionUsesUtc(): void
    {
        $pdo = TestDb::connect();
        $query = $pdo->query('SELECT @@session.time_zone');
        self::assertSame('+00:00', $query === false ? null : $query->fetchColumn());
    }
}
```

- [ ] **Step 6: Run unit and integration tests**

Run: `docker compose run --rm app composer test`
Expected: all pass, including `MigrationTest` (the `db` service starts automatically).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Add database connection, migrator, transactions and schema"
```

---

### Task 6: Venue settings and stations

**Files:** Create `src/Settings/VenueSettings.php`, `src/Settings/SettingsRepository.php`, `src/Domain/StationRepository.php`, `tests/Unit/Settings/VenueSettingsTest.php`, `tests/Integration/SettingsAndStationsTest.php`.

- [ ] **Step 1: Write the failing tests**

`tests/Unit/Settings/VenueSettingsTest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Settings;

use OpenArcade\Settings\VenueSettings;
use PHPUnit\Framework\TestCase;

final class VenueSettingsTest extends TestCase
{
    public function testDefaultsFillMissingValues(): void
    {
        $settings = VenueSettings::fromArray(['venue_name' => 'Orbit VR', 'tax_rate_bp' => '935']);
        self::assertSame('Orbit VR', $settings->venueName);
        self::assertSame(935, $settings->taxRateBp);
        self::assertSame(30, $settings->slotStepMinutes);
        self::assertSame(10, $settings->bufferMinutes);
        self::assertSame('America/Chicago', $settings->tz()->getName());
    }

    public function testRejectsInvalidTimezoneAndOutOfRangeNumbers(): void
    {
        foreach ([['timezone' => 'Mars/Olympus'], ['slot_step_minutes' => '0'], ['buffer_minutes' => '-5'], ['currency' => 'usd'], ['tax_rate_bp' => '10001']] as $bad) {
            try {
                VenueSettings::fromArray($bad);
                self::fail('expected InvalidArgumentException for ' . json_encode($bad));
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }
}
```

`tests/Integration/SettingsAndStationsTest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\StationRepository;
use OpenArcade\Settings\SettingsRepository;
use PHPUnit\Framework\TestCase;

final class SettingsAndStationsTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testSettingsRoundTrip(): void
    {
        $repo = new SettingsRepository(TestDb::fresh());
        self::assertSame('My VR Arcade', $repo->load()->venueName);
        $repo->set('venue_name', 'Orbit VR');
        $repo->set('buffer_minutes', '15');
        $loaded = $repo->load();
        self::assertSame('Orbit VR', $loaded->venueName);
        self::assertSame(15, $loaded->bufferMinutes);
    }

    public function testSetRejectsUnknownKeysAndInvalidValues(): void
    {
        $repo = new SettingsRepository(TestDb::fresh());
        foreach ([['nonsense', '1'], ['timezone', 'Mars/Olympus']] as [$key, $value]) {
            try {
                $repo->set($key, $value);
                self::fail('expected InvalidArgumentException');
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }

    public function testSyncCountCreatesAndDeactivatesStations(): void
    {
        $stations = new StationRepository(TestDb::fresh());
        $stations->syncCount(4);
        self::assertSame([1, 2, 3, 4], array_values($stations->activeNumbersById()));
        $stations->syncCount(2);
        self::assertSame([1, 2], array_values($stations->activeNumbersById()));
        $stations->syncCount(3);
        self::assertSame([1, 2, 3], array_values($stations->activeNumbersById()));
    }
}
```

- [ ] **Step 2: Write `src/Settings/VenueSettings.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Settings;

final class VenueSettings
{
    public const DEFAULTS = [
        'venue_name' => 'My VR Arcade',
        'timezone' => 'America/Chicago',
        'currency' => 'USD',
        'tax_rate_bp' => '0',
        'slot_step_minutes' => '30',
        'buffer_minutes' => '10',
        'min_lead_minutes' => '30',
        'max_advance_days' => '90',
        'hold_minutes' => '10',
        'notification_email' => '',
    ];

    private function __construct(
        public readonly string $venueName,
        public readonly string $timezone,
        public readonly string $currency,
        public readonly int $taxRateBp,
        public readonly int $slotStepMinutes,
        public readonly int $bufferMinutes,
        public readonly int $minLeadMinutes,
        public readonly int $maxAdvanceDays,
        public readonly int $holdMinutes,
        public readonly string $notificationEmail,
    ) {
    }

    /** @param array<string,string> $values */
    public static function fromArray(array $values): self
    {
        $v = array_merge(self::DEFAULTS, array_intersect_key($values, self::DEFAULTS));

        $name = trim($v['venue_name']);
        if ($name === '' || mb_strlen($name) > 80) {
            throw new \InvalidArgumentException('venue_name must be 1-80 characters.');
        }
        if (!in_array($v['timezone'], \DateTimeZone::listIdentifiers(), true)) {
            throw new \InvalidArgumentException('timezone must be a valid IANA timezone, for example America/Chicago.');
        }
        if (preg_match('/^[A-Z]{3}$/', $v['currency']) !== 1) {
            throw new \InvalidArgumentException('currency must be a 3-letter upper-case code.');
        }
        $email = trim($v['notification_email']);
        if ($email !== '' && filter_var($email, FILTER_VALIDATE_EMAIL) === false) {
            throw new \InvalidArgumentException('notification_email is not a valid email address.');
        }

        return new self(
            $name,
            $v['timezone'],
            $v['currency'],
            self::intInRange($v, 'tax_rate_bp', 0, 10000),
            self::intInRange($v, 'slot_step_minutes', 5, 240),
            self::intInRange($v, 'buffer_minutes', 0, 240),
            self::intInRange($v, 'min_lead_minutes', 0, 10080),
            self::intInRange($v, 'max_advance_days', 1, 730),
            self::intInRange($v, 'hold_minutes', 1, 120),
            $email,
        );
    }

    public function tz(): \DateTimeZone
    {
        return new \DateTimeZone($this->timezone);
    }

    /** @param array<string,string> $values */
    private static function intInRange(array $values, string $key, int $min, int $max): int
    {
        $raw = $values[$key];
        if (preg_match('/^-?\d+$/', $raw) !== 1 || (int) $raw < $min || (int) $raw > $max) {
            throw new \InvalidArgumentException("{$key} must be a whole number from {$min} to {$max}.");
        }

        return (int) $raw;
    }
}
```

- [ ] **Step 3: Write `src/Settings/SettingsRepository.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Settings;

use PDO;

final class SettingsRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    /** @return array<string,string> */
    public function all(): array
    {
        $query = $this->pdo->query('SELECT `key`, `value` FROM settings');
        $rows = $query === false ? [] : $query->fetchAll(PDO::FETCH_KEY_PAIR);

        return array_map('strval', $rows);
    }

    public function load(): VenueSettings
    {
        return VenueSettings::fromArray($this->all());
    }

    public function set(string $key, string $value): void
    {
        if (!array_key_exists($key, VenueSettings::DEFAULTS)) {
            throw new \InvalidArgumentException("Unknown setting {$key}.");
        }
        // Validate the whole set with the new value before saving it.
        VenueSettings::fromArray(array_merge($this->all(), [$key => $value]));
        $this->pdo->prepare('INSERT INTO settings (`key`, `value`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `value` = VALUES(`value`)')
            ->execute([$key, $value]);
    }
}
```

- [ ] **Step 4: Write `src/Domain/StationRepository.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

use PDO;

final class StationRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    /** @return array<int,int> station id => station number, active stations only, ordered by number */
    public function activeNumbersById(): array
    {
        $query = $this->pdo->query('SELECT id, number FROM stations WHERE active = 1 ORDER BY number');
        $result = [];
        foreach ($query === false ? [] : $query->fetchAll() as $row) {
            $result[(int) $row['id']] = (int) $row['number'];
        }

        return $result;
    }

    /** Makes stations 1..$count active (creating them when needed) and deactivates the rest. */
    public function syncCount(int $count): void
    {
        if ($count < 1 || $count > 200) {
            throw new \InvalidArgumentException('Station count must be from 1 to 200.');
        }
        $insert = $this->pdo->prepare(
            'INSERT INTO stations (number, label, active) VALUES (?, ?, 1) ON DUPLICATE KEY UPDATE active = 1'
        );
        for ($number = 1; $number <= $count; $number++) {
            $insert->execute([$number, 'Station ' . $number]);
        }
        $this->pdo->prepare('UPDATE stations SET active = 0 WHERE number > ?')->execute([$count]);
    }
}
```

- [ ] **Step 5: Run the tests**

Run: `docker compose run --rm app composer test`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Add venue settings and station repository"
```

---

### Task 7: Opening hours

**Files:** Create `src/Domain/DayHours.php`, `src/Domain/HoursRepository.php`, `tests/Unit/Domain/DayHoursTest.php`, `tests/Integration/HoursRepositoryTest.php`.

- [ ] **Step 1: Write the failing tests**

`tests/Unit/Domain/DayHoursTest.php`:
```php
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
```

`tests/Integration/HoursRepositoryTest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\HoursRepository;
use PHPUnit\Framework\TestCase;

final class HoursRepositoryTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testPrecedenceClosedDateThenSpecialHoursThenWeekday(): void
    {
        $pdo = TestDb::fresh();
        $repo = new HoursRepository($pdo);
        // 2026-09-21 is a Monday (weekday 1), 2026-09-22 a Tuesday (2), 2026-09-23 a Wednesday (3).
        $repo->setWeekday(1, 600, 1320, false);
        $repo->setWeekday(2, 600, 1320, true);
        $repo->setWeekday(3, 600, 1320, false);
        $repo->setSpecialHours('2026-09-23', 720, 1080);
        $repo->addClosedDate('2026-09-28', 'Private event');
        $repo->setWeekday(0, 600, 1320, false);

        $monday = $repo->forDate('2026-09-21');
        self::assertNotNull($monday);
        self::assertSame([600, 1320], [$monday->openMinute, $monday->closeMinute]);
        self::assertNull($repo->forDate('2026-09-22'), 'weekday marked closed');
        $special = $repo->forDate('2026-09-23');
        self::assertNotNull($special);
        self::assertSame([720, 1080], [$special->openMinute, $special->closeMinute]);
        self::assertNull($repo->forDate('2026-09-28'), 'closed date wins over weekday hours');
        self::assertNull($repo->forDate('2026-09-24'), 'no row for Thursday means closed');
    }

    public function testRejectsMalformedDate(): void
    {
        $this->expectException(\InvalidArgumentException::class);
        (new HoursRepository(TestDb::fresh()))->forDate('09/21/2026');
    }
}
```

- [ ] **Step 2: Write `src/Domain/DayHours.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

/** Opening hours for one local date, as minutes after local midnight. Sessions never cross midnight. */
final class DayHours
{
    public function __construct(public readonly int $openMinute, public readonly int $closeMinute)
    {
        if ($openMinute < 0 || $closeMinute > 1440 || $openMinute >= $closeMinute) {
            throw new \InvalidArgumentException('Hours must satisfy 0 <= open < close <= 1440.');
        }
    }
}
```

- [ ] **Step 3: Write `src/Domain/HoursRepository.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

use PDO;

final class HoursRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    public static function assertDate(string $localDate): void
    {
        $parsed = \DateTimeImmutable::createFromFormat('!Y-m-d', $localDate);
        if ($parsed === false || $parsed->format('Y-m-d') !== $localDate) {
            throw new \InvalidArgumentException('Date must be YYYY-MM-DD.');
        }
    }

    /** Hours for a local date, or null when the venue is closed that day. */
    public function forDate(string $localDate): ?DayHours
    {
        self::assertDate($localDate);

        $closed = $this->pdo->prepare('SELECT 1 FROM closed_dates WHERE local_date = ?');
        $closed->execute([$localDate]);
        if ($closed->fetchColumn() !== false) {
            return null;
        }

        $special = $this->pdo->prepare('SELECT open_minute, close_minute FROM special_hours WHERE local_date = ?');
        $special->execute([$localDate]);
        $row = $special->fetch();
        if ($row !== false) {
            return new DayHours((int) $row['open_minute'], (int) $row['close_minute']);
        }

        $weekday = (int) (new \DateTimeImmutable($localDate))->format('w');
        $regular = $this->pdo->prepare('SELECT open_minute, close_minute, closed FROM business_hours WHERE weekday = ?');
        $regular->execute([$weekday]);
        $row = $regular->fetch();
        if ($row === false || (int) $row['closed'] === 1) {
            return null;
        }

        return new DayHours((int) $row['open_minute'], (int) $row['close_minute']);
    }

    public function setWeekday(int $weekday, int $openMinute, int $closeMinute, bool $closed): void
    {
        if ($weekday < 0 || $weekday > 6) {
            throw new \InvalidArgumentException('Weekday must be 0 (Sunday) to 6 (Saturday).');
        }
        new DayHours($openMinute, $closeMinute);
        $this->pdo->prepare(
            'INSERT INTO business_hours (weekday, open_minute, close_minute, closed) VALUES (?, ?, ?, ?) '
            . 'ON DUPLICATE KEY UPDATE open_minute = VALUES(open_minute), close_minute = VALUES(close_minute), closed = VALUES(closed)'
        )->execute([$weekday, $openMinute, $closeMinute, $closed ? 1 : 0]);
    }

    public function setSpecialHours(string $localDate, int $openMinute, int $closeMinute): void
    {
        self::assertDate($localDate);
        new DayHours($openMinute, $closeMinute);
        $this->pdo->prepare(
            'INSERT INTO special_hours (local_date, open_minute, close_minute) VALUES (?, ?, ?) '
            . 'ON DUPLICATE KEY UPDATE open_minute = VALUES(open_minute), close_minute = VALUES(close_minute)'
        )->execute([$localDate, $openMinute, $closeMinute]);
    }

    public function addClosedDate(string $localDate, string $reason): void
    {
        self::assertDate($localDate);
        $this->pdo->prepare(
            'INSERT INTO closed_dates (local_date, reason) VALUES (?, ?) ON DUPLICATE KEY UPDATE reason = VALUES(reason)'
        )->execute([$localDate, mb_substr($reason, 0, 120)]);
    }
}
```

- [ ] **Step 4: Run the tests**

Run: `docker compose run --rm app composer test`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Add opening hours with closed dates and special hours"
```

---

### Task 8: Pricing

**Files:** Create `src/Domain/Quote.php`, `src/Domain/PriceList.php`, `src/Domain/PriceRepository.php`, `tests/Unit/Domain/PriceListTest.php`, `tests/Integration/PriceRepositoryTest.php`.

- [ ] **Step 1: Write the failing tests**

`tests/Unit/Domain/PriceListTest.php`:
```php
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
```

`tests/Integration/PriceRepositoryTest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\PriceRepository;
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
```

- [ ] **Step 2: Write `src/Domain/Quote.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class Quote
{
    public function __construct(
        public readonly int $subtotalCents,
        public readonly int $taxCents,
        public readonly int $totalCents,
        public readonly string $currency,
    ) {
    }
}
```

- [ ] **Step 3: Write `src/Domain/PriceList.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

use OpenArcade\Support\Money;

final class PriceList
{
    public const EVERY_DAY = -1;

    /** @param array<int, array<int,int>> $centsByWeekday weekday (-1 = every day, 0..6) => [duration minutes => cents per station] */
    public function __construct(private array $centsByWeekday)
    {
    }

    public function priceCents(int $weekday, int $durationMinutes): ?int
    {
        return $this->centsByWeekday[$weekday][$durationMinutes]
            ?? $this->centsByWeekday[self::EVERY_DAY][$durationMinutes]
            ?? null;
    }

    /** @return int[] durations offered on that weekday, ascending */
    public function durations(int $weekday): array
    {
        $durations = array_keys(
            ($this->centsByWeekday[self::EVERY_DAY] ?? []) + ($this->centsByWeekday[$weekday] ?? [])
        );
        sort($durations);

        return $durations;
    }

    public function quote(int $weekday, int $durationMinutes, int $stationCount, int $taxRateBp, string $currency): Quote
    {
        $price = $this->priceCents($weekday, $durationMinutes);
        if ($price === null) {
            throw new \InvalidArgumentException('Duration is not offered on that day.');
        }
        if ($stationCount < 1) {
            throw new \InvalidArgumentException('Station count must be at least 1.');
        }
        $subtotal = Money::of($price, $currency)->times($stationCount);
        $tax = $subtotal->taxAt($taxRateBp);

        return new Quote($subtotal->cents, $tax->cents, $subtotal->plus($tax)->cents, $currency);
    }
}
```

- [ ] **Step 4: Write `src/Domain/PriceRepository.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

use PDO;

final class PriceRepository
{
    public function __construct(private PDO $pdo)
    {
    }

    public function load(): PriceList
    {
        $query = $this->pdo->query('SELECT weekday, duration_minutes, price_cents FROM prices');
        $byWeekday = [];
        foreach ($query === false ? [] : $query->fetchAll() as $row) {
            $byWeekday[(int) $row['weekday']][(int) $row['duration_minutes']] = (int) $row['price_cents'];
        }

        return new PriceList($byWeekday);
    }

    public function set(int $weekday, int $durationMinutes, int $priceCents): void
    {
        $this->assertValid($weekday, $durationMinutes);
        if ($priceCents < 0) {
            throw new \InvalidArgumentException('Price cannot be negative.');
        }
        $this->pdo->prepare(
            'INSERT INTO prices (weekday, duration_minutes, price_cents) VALUES (?, ?, ?) '
            . 'ON DUPLICATE KEY UPDATE price_cents = VALUES(price_cents)'
        )->execute([$weekday, $durationMinutes, $priceCents]);
    }

    public function remove(int $weekday, int $durationMinutes): void
    {
        $this->assertValid($weekday, $durationMinutes);
        $this->pdo->prepare('DELETE FROM prices WHERE weekday = ? AND duration_minutes = ?')
            ->execute([$weekday, $durationMinutes]);
    }

    private function assertValid(int $weekday, int $durationMinutes): void
    {
        if ($weekday < PriceList::EVERY_DAY || $weekday > 6) {
            throw new \InvalidArgumentException('Weekday must be -1 (every day) or 0 (Sunday) to 6 (Saturday).');
        }
        if ($durationMinutes < 5 || $durationMinutes > 1440) {
            throw new \InvalidArgumentException('Duration must be from 5 to 1440 minutes.');
        }
    }
}
```

- [ ] **Step 5: Run the tests**

Run: `docker compose run --rm app composer test`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Add pricing with weekday overrides and tax"
```

---

### Task 9: Availability engine

Pure logic, no database. Two sessions on one station must be at least `buffer` minutes apart: a new session `[start, end)` conflicts with an existing block `[bs, be)` when `start < be + buffer` and `bs < end + buffer`. The slot grid is anchored at the opening minute.

**Files:** Create `src/Domain/Block.php`, `src/Domain/Slot.php`, `src/Domain/Availability.php`, `tests/Unit/Domain/AvailabilityTest.php`.

- [ ] **Step 1: Write the failing test `tests/Unit/Domain/AvailabilityTest.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Domain;

use OpenArcade\Domain\Availability;
use OpenArcade\Domain\Block;
use OpenArcade\Domain\DayHours;
use OpenArcade\Domain\Slot;
use PHPUnit\Framework\TestCase;

final class AvailabilityTest extends TestCase
{
    /**
     * @param Slot[] $slots
     * @return array<int, int[]> start minute => free station ids
     */
    private function asMap(array $slots): array
    {
        $map = [];
        foreach ($slots as $slot) {
            $map[$slot->startMinute] = $slot->freeStationIds;
        }

        return $map;
    }

    public function testGridCoversOpeningHoursAndLastSlotEndsAtClose(): void
    {
        $slots = Availability::slots(new DayHours(600, 720), [1, 2], [], 60, 30, 0);
        self::assertSame([600 => [1, 2], 630 => [1, 2], 660 => [1, 2]], $this->asMap($slots));
    }

    public function testBlockWithBufferRemovesStationFromConflictingSlots(): void
    {
        $blocks = [new Block(1, 630, 690)];
        $map = $this->asMap(Availability::slots(new DayHours(600, 780), [1, 2], $blocks, 60, 30, 10));
        self::assertSame([2], $map[600]);
        self::assertSame([2], $map[630]);
        self::assertSame([2], $map[660]);
        self::assertSame([2], $map[690], 'starts inside the 10 minute buffer after the block');
        self::assertSame([1, 2], $map[720]);
    }

    public function testBackToBackIsAllowedOnlyWithZeroBuffer(): void
    {
        $blocks = [new Block(1, 600, 660)];
        self::assertSame([1], Availability::freeStations([1], $blocks, 660, 720, 0));
        self::assertSame([], Availability::freeStations([1], $blocks, 660, 720, 10));
        self::assertSame([1], Availability::freeStations([1], $blocks, 670, 730, 10));
    }

    public function testBufferAlsoAppliesBeforeAnExistingBlock(): void
    {
        $blocks = [new Block(1, 700, 760)];
        self::assertSame([], Availability::freeStations([1], $blocks, 635, 695, 10));
        self::assertSame([1], Availability::freeStations([1], $blocks, 630, 690, 10));
    }

    public function testEarliestStartIsRoundedUpToTheGrid(): void
    {
        $slots = Availability::slots(new DayHours(600, 780), [1], [], 60, 30, 0, 645);
        self::assertSame([660, 690, 720], array_keys($this->asMap($slots)));
    }

    public function testDurationLongerThanTheDayGivesNoSlots(): void
    {
        self::assertSame([], Availability::slots(new DayHours(600, 660), [1], [], 90, 30, 0));
    }

    public function testBlocksForUnknownStationsAreIgnored(): void
    {
        $slots = Availability::slots(new DayHours(600, 660), [1], [new Block(99, 600, 660)], 60, 30, 0);
        self::assertSame([600 => [1]], $this->asMap($slots));
    }

    public function testRejectsNonPositiveStepOrDuration(): void
    {
        foreach ([[0, 30], [60, 0]] as [$duration, $step]) {
            try {
                Availability::slots(new DayHours(600, 720), [1], [], $duration, $step, 0);
                self::fail('expected InvalidArgumentException');
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }
}
```

- [ ] **Step 2: Run to see it fail**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Domain/AvailabilityTest.php`
Expected: FAIL, classes not found.

- [ ] **Step 3: Write `src/Domain/Block.php` and `src/Domain/Slot.php`**

`src/Domain/Block.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

/** Time one station is taken on one local date, as minutes after local midnight. */
final class Block
{
    public function __construct(
        public readonly int $stationId,
        public readonly int $startMinute,
        public readonly int $endMinute,
    ) {
    }
}
```

`src/Domain/Slot.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class Slot
{
    /** @param int[] $freeStationIds */
    public function __construct(public readonly int $startMinute, public readonly array $freeStationIds)
    {
    }
}
```

- [ ] **Step 4: Write `src/Domain/Availability.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class Availability
{
    /**
     * Every start time on the slot grid that fits in the opening hours, with the stations free for it.
     *
     * @param int[] $stationIds active station ids
     * @param Block[] $blocks blocking sessions on that date
     * @return Slot[]
     */
    public static function slots(
        DayHours $hours,
        array $stationIds,
        array $blocks,
        int $durationMinutes,
        int $slotStepMinutes,
        int $bufferMinutes,
        int $earliestStartMinute = 0,
    ): array {
        if ($durationMinutes < 1 || $slotStepMinutes < 1 || $bufferMinutes < 0) {
            throw new \InvalidArgumentException('Duration and slot step must be positive; buffer cannot be negative.');
        }

        $first = $hours->openMinute;
        if ($earliestStartMinute > $first) {
            $stepsToSkip = intdiv($earliestStartMinute - $first + $slotStepMinutes - 1, $slotStepMinutes);
            $first += $stepsToSkip * $slotStepMinutes;
        }

        $slots = [];
        for ($start = $first; $start + $durationMinutes <= $hours->closeMinute; $start += $slotStepMinutes) {
            $slots[] = new Slot(
                $start,
                self::freeStations($stationIds, $blocks, $start, $start + $durationMinutes, $bufferMinutes)
            );
        }

        return $slots;
    }

    /**
     * Stations with no blocking session within $bufferMinutes of [$startMinute, $endMinute).
     *
     * @param int[] $stationIds
     * @param Block[] $blocks
     * @return int[] in the order given
     */
    public static function freeStations(array $stationIds, array $blocks, int $startMinute, int $endMinute, int $bufferMinutes): array
    {
        $taken = [];
        foreach ($blocks as $block) {
            if ($startMinute < $block->endMinute + $bufferMinutes && $block->startMinute < $endMinute + $bufferMinutes) {
                $taken[$block->stationId] = true;
            }
        }

        return array_values(array_filter($stationIds, static fn (int $id): bool => !isset($taken[$id])));
    }
}
```

- [ ] **Step 5: Run the test**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Domain/AvailabilityTest.php`
Expected: `OK (8 tests, ...)`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Add availability engine with buffers and slot grid"
```

---

### Task 10: Station allocator

The server, never the browser, picks stations. Prefer the free station whose previous session ended closest to the new start (tight packing keeps other stations open for long bookings). Ties go to the lowest station number.

**Files:** Create `src/Domain/StationAllocator.php`, `tests/Unit/Domain/StationAllocatorTest.php`.

- [ ] **Step 1: Write the failing test**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Domain;

use OpenArcade\Domain\Block;
use OpenArcade\Domain\StationAllocator;
use PHPUnit\Framework\TestCase;

final class StationAllocatorTest extends TestCase
{
    public function testReturnsNullWhenNotEnoughStationsAreFree(): void
    {
        self::assertNull(StationAllocator::choose([1], [], [1 => 1], 600, 2, 540));
    }

    public function testPrefersTheStationWithTheSmallestIdleGap(): void
    {
        $numbers = [1 => 1, 2 => 2, 3 => 3];
        $blocks = [new Block(2, 530, 590), new Block(3, 500, 560)];
        // gaps before 600: station 2 = 10, station 3 = 40, station 1 = 60 (idle since opening at 540)
        self::assertSame([2], StationAllocator::choose([1, 2, 3], $blocks, $numbers, 600, 1, 540));
        self::assertSame([2, 3], StationAllocator::choose([1, 2, 3], $blocks, $numbers, 600, 2, 540));
    }

    public function testTiesGoToTheLowestStationNumberNotTheLowestId(): void
    {
        $numbers = [10 => 2, 11 => 1];
        self::assertSame([11], StationAllocator::choose([10, 11], [], $numbers, 600, 1, 540));
    }

    public function testLaterBlocksDoNotAffectTheGap(): void
    {
        $numbers = [1 => 1, 2 => 2];
        $blocks = [new Block(2, 700, 760)];
        self::assertSame([1], StationAllocator::choose([1, 2], $blocks, $numbers, 600, 1, 540));
    }
}
```

- [ ] **Step 2: Write `src/Domain/StationAllocator.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class StationAllocator
{
    /**
     * @param int[] $freeStationIds stations free for the requested window
     * @param Block[] $blocks all blocking sessions on that date
     * @param array<int,int> $numbersById station id => station number
     * @return int[]|null chosen station ids ordered by station number, or null when too few are free
     */
    public static function choose(
        array $freeStationIds,
        array $blocks,
        array $numbersById,
        int $startMinute,
        int $count,
        int $openMinute,
    ): ?array {
        if ($count < 1 || count($freeStationIds) < $count) {
            return null;
        }

        $candidates = [];
        foreach ($freeStationIds as $id) {
            $previousEnd = min($openMinute, $startMinute);
            foreach ($blocks as $block) {
                if ($block->stationId === $id && $block->endMinute <= $startMinute && $block->endMinute > $previousEnd) {
                    $previousEnd = $block->endMinute;
                }
            }
            $candidates[] = [
                'id' => $id,
                'gap' => $startMinute - $previousEnd,
                'number' => $numbersById[$id] ?? PHP_INT_MAX,
            ];
        }
        usort($candidates, static fn (array $a, array $b): int => [$a['gap'], $a['number']] <=> [$b['gap'], $b['number']]);

        $chosen = array_slice($candidates, 0, $count);
        usort($chosen, static fn (array $a, array $b): int => $a['number'] <=> $b['number']);

        return array_column($chosen, 'id');
    }
}
```

- [ ] **Step 3: Run the test**

Run: `docker compose run --rm app vendor/bin/phpunit tests/Unit/Domain/StationAllocatorTest.php`
Expected: `OK (4 tests, ...)`.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Add best-fit station allocator"
```

---

### Task 11: Reservation write path

The only way a reservation is written. Writers for one date are serialised: the `booking_days` row is created with an autocommit `INSERT IGNORE` *before* the transaction (so no shared lock is left behind), then locked with `SELECT ... FOR UPDATE` inside it. Availability is recomputed under the lock, stations are allocated by the server, the price is computed by the server.

**Files:** Create `src/Domain/BookingRequest.php`, `BookingRules.php`, `BookingRejected.php`, `Reservation.php`, `ReservationRepository.php`, `Reservations.php`, `tests/Integration/VenueFixture.php`, `tests/Integration/ReservationsTest.php`.

- [ ] **Step 1: Write `src/Domain/BookingRejected.php`, `BookingRules.php`, `BookingRequest.php`, `Reservation.php`**

`src/Domain/BookingRejected.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

/**
 * A booking the system refuses. $reason is a stable machine code:
 * validation_failed, date_in_past, too_far_ahead, closed, outside_hours, off_grid, too_soon,
 * duration_not_offered, invalid_station_count, slot_unavailable, hold_expired, not_found, wrong_status.
 */
final class BookingRejected extends \DomainException
{
    /** @param array<string,string> $fieldErrors */
    public function __construct(public readonly string $reason, public readonly array $fieldErrors = [])
    {
        parent::__construct($reason);
    }
}
```

`src/Domain/BookingRules.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class BookingRules
{
    private function __construct(
        public readonly bool $enforceHours,
        public readonly bool $enforceGrid,
        public readonly bool $enforceLeadTime,
        public readonly bool $enforceAdvanceLimit,
        public readonly bool $contactRequired,
        public readonly bool $requirePayment,
        public readonly bool $complimentary,
        public readonly string $createdBy,
    ) {
    }

    /** A booking made by the public. */
    public static function customer(bool $requirePayment): self
    {
        return new self(true, true, true, true, true, $requirePayment, false, 'customer');
    }

    /** A booking made by logged-in staff: walk-ins, off-grid starts, outside hours. Overlaps are still refused. */
    public static function admin(bool $complimentary = false): self
    {
        return new self(false, false, false, false, false, false, $complimentary, 'admin');
    }
}
```

`src/Domain/BookingRequest.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class BookingRequest
{
    public function __construct(
        public readonly string $localDate,
        public readonly int $startMinute,
        public readonly int $durationMinutes,
        public readonly int $stationCount,
        public readonly string $firstName,
        public readonly string $lastName,
        public readonly string $email,
        public readonly string $phone,
        public readonly ?string $comments = null,
    ) {
    }

    /** @return array<string,string> field => problem; empty when valid */
    public function validate(bool $contactRequired): array
    {
        $errors = [];
        $date = \DateTimeImmutable::createFromFormat('!Y-m-d', $this->localDate);
        if ($date === false || $date->format('Y-m-d') !== $this->localDate) {
            $errors['date'] = 'Date must be YYYY-MM-DD.';
        }
        if ($this->startMinute < 0 || $this->startMinute > 1439) {
            $errors['start'] = 'Start time is not valid.';
        }
        if ($this->durationMinutes < 5 || $this->durationMinutes > 1440) {
            $errors['duration'] = 'Duration is not valid.';
        }
        foreach (['first_name' => $this->firstName, 'last_name' => $this->lastName] as $field => $value) {
            $length = mb_strlen(trim($value));
            if ($length < 1 || $length > 60) {
                $errors[$field] = 'Must be 1 to 60 characters.';
            }
        }
        $email = trim($this->email);
        $emailBad = $email === ''
            ? $contactRequired
            : (mb_strlen($email) > 190 || filter_var($email, FILTER_VALIDATE_EMAIL) === false);
        if ($emailBad) {
            $errors['email'] = 'Enter a valid email address.';
        }
        $phone = trim($this->phone);
        $phoneBad = $phone === '' ? $contactRequired : preg_match('/^[0-9+()\-. ]{7,30}$/', $phone) !== 1;
        if ($phoneBad) {
            $errors['phone'] = 'Enter a valid phone number.';
        }
        if ($this->comments !== null && mb_strlen($this->comments) > 1000) {
            $errors['comments'] = 'Must be 1000 characters or fewer.';
        }

        return $errors;
    }
}
```

`src/Domain/Reservation.php`:
```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

final class Reservation
{
    /** @param int[] $stationIds */
    public function __construct(
        public readonly int $id,
        public readonly string $uuid,
        public readonly string $confirmationCode,
        public readonly string $status,
        public readonly string $localDate,
        public readonly int $startMinute,
        public readonly int $durationMinutes,
        public readonly array $stationIds,
        public readonly string $startUtc,
        public readonly string $endUtc,
        public readonly int $subtotalCents,
        public readonly int $taxCents,
        public readonly int $totalCents,
        public readonly string $currency,
        public readonly ?string $holdExpiresAtUtc,
        public readonly ?string $paymentId,
    ) {
    }
}
```

- [ ] **Step 2: Write `src/Domain/ReservationRepository.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

use PDO;

final class ReservationRepository
{
    private const CODE_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

    public function __construct(private PDO $pdo)
    {
    }

    /** Call OUTSIDE a transaction, so the shared lock taken by the duplicate check is released at once. */
    public function ensureDayRow(string $localDate): void
    {
        $this->pdo->prepare('INSERT IGNORE INTO booking_days (local_date) VALUES (?)')->execute([$localDate]);
    }

    /** Call INSIDE a transaction. Blocks until no other writer holds this date. */
    public function lockDay(string $localDate): void
    {
        $statement = $this->pdo->prepare('SELECT local_date FROM booking_days WHERE local_date = ? FOR UPDATE');
        $statement->execute([$localDate]);
        if ($statement->fetchColumn() === false) {
            throw new \LogicException('ensureDayRow() must be called before lockDay().');
        }
    }

    /**
     * Sessions that block stations on a date: confirmed ones, and holds that have not expired.
     *
     * @return Block[]
     */
    public function blocksForDate(string $localDate, \DateTimeImmutable $nowUtc, \DateTimeZone $tz, ?int $excludeReservationId = null): array
    {
        $sql = 'SELECT rs.station_id, r.start_utc, r.end_utc FROM reservations r '
            . 'JOIN reservation_stations rs ON rs.reservation_id = r.id '
            . "WHERE r.local_date = ? AND (r.status = 'confirmed' OR (r.status = 'held' AND r.hold_expires_at > ?))";
        $params = [$localDate, $nowUtc->format('Y-m-d H:i:s')];
        if ($excludeReservationId !== null) {
            $sql .= ' AND r.id <> ?';
            $params[] = $excludeReservationId;
        }
        $statement = $this->pdo->prepare($sql);
        $statement->execute($params);

        $blocks = [];
        foreach ($statement->fetchAll() as $row) {
            [$start, $end] = self::localMinutes((string) $row['start_utc'], (string) $row['end_utc'], $tz);
            $blocks[] = new Block((int) $row['station_id'], $start, $end);
        }

        return $blocks;
    }

    /**
     * @param array<string, int|string|null> $fields column => value, without uuid and confirmation_code
     * @param int[] $stationIds
     */
    public function insert(array $fields, array $stationIds): int
    {
        $fields['uuid'] = self::uuid();
        for ($attempt = 1; ; $attempt++) {
            $fields['confirmation_code'] = self::confirmationCode();
            $columns = array_keys($fields);
            $sql = 'INSERT INTO reservations (' . implode(', ', $columns) . ') VALUES ('
                . implode(', ', array_fill(0, count($columns), '?')) . ')';
            try {
                $this->pdo->prepare($sql)->execute(array_values($fields));
                break;
            } catch (\PDOException $error) {
                $duplicateCode = (int) ($error->errorInfo[1] ?? 0) === 1062
                    && str_contains($error->getMessage(), 'uq_reservations_code');
                if (!$duplicateCode || $attempt >= 5) {
                    throw $error;
                }
            }
        }
        $id = (int) $this->pdo->lastInsertId();
        $link = $this->pdo->prepare('INSERT INTO reservation_stations (reservation_id, station_id) VALUES (?, ?)');
        foreach ($stationIds as $stationId) {
            $link->execute([$id, $stationId]);
        }

        return $id;
    }

    public function find(int $id, \DateTimeZone $tz, bool $forUpdate = false): ?Reservation
    {
        $statement = $this->pdo->prepare('SELECT * FROM reservations WHERE id = ?' . ($forUpdate ? ' FOR UPDATE' : ''));
        $statement->execute([$id]);
        $row = $statement->fetch();
        if ($row === false) {
            return null;
        }
        $stations = $this->pdo->prepare('SELECT station_id FROM reservation_stations WHERE reservation_id = ? ORDER BY station_id');
        $stations->execute([$id]);
        [$startMinute] = self::localMinutes((string) $row['start_utc'], (string) $row['end_utc'], $tz);

        return new Reservation(
            (int) $row['id'],
            (string) $row['uuid'],
            (string) $row['confirmation_code'],
            (string) $row['status'],
            (string) $row['local_date'],
            $startMinute,
            (int) $row['duration_minutes'],
            array_map('intval', $stations->fetchAll(PDO::FETCH_COLUMN)),
            (string) $row['start_utc'],
            (string) $row['end_utc'],
            (int) $row['subtotal_cents'],
            (int) $row['tax_cents'],
            (int) $row['total_cents'],
            (string) $row['currency'],
            $row['hold_expires_at'] === null ? null : (string) $row['hold_expires_at'],
            $row['payment_id'] === null ? null : (string) $row['payment_id'],
        );
    }

    /**
     * @param string[] $fromStatuses
     * @return bool true when a row changed
     */
    public function transition(int $id, array $fromStatuses, string $toStatus, \DateTimeImmutable $nowUtc, ?string $provider = null, ?string $paymentId = null): bool
    {
        $placeholders = implode(', ', array_fill(0, count($fromStatuses), '?'));
        $sql = 'UPDATE reservations SET status = ?, updated_at = ?, hold_expires_at = NULL';
        $params = [$toStatus, $nowUtc->format('Y-m-d H:i:s')];
        if ($provider !== null) {
            $sql .= ', payment_provider = ?, payment_id = ?';
            $params[] = $provider;
            $params[] = $paymentId;
        }
        $sql .= " WHERE id = ? AND status IN ({$placeholders})";
        $statement = $this->pdo->prepare($sql);
        $statement->execute([...$params, $id, ...$fromStatuses]);

        return $statement->rowCount() === 1;
    }

    public function expireHolds(\DateTimeImmutable $nowUtc): int
    {
        $statement = $this->pdo->prepare(
            "UPDATE reservations SET status = 'expired', updated_at = ?, hold_expires_at = NULL "
            . "WHERE status = 'held' AND hold_expires_at <= ?"
        );
        $now = $nowUtc->format('Y-m-d H:i:s');
        $statement->execute([$now, $now]);

        return $statement->rowCount();
    }

    /** @return array{0:int,1:int} start and end as minutes after local midnight of the start date */
    private static function localMinutes(string $startUtc, string $endUtc, \DateTimeZone $tz): array
    {
        $utc = new \DateTimeZone('UTC');
        $start = (new \DateTimeImmutable($startUtc, $utc))->setTimezone($tz);
        $end = (new \DateTimeImmutable($endUtc, $utc))->setTimezone($tz);
        $startMinute = (int) $start->format('G') * 60 + (int) $start->format('i');
        $endMinute = $end->format('Y-m-d') > $start->format('Y-m-d')
            ? 1440
            : (int) $end->format('G') * 60 + (int) $end->format('i');

        return [$startMinute, $endMinute];
    }

    private static function uuid(): string
    {
        $bytes = random_bytes(16);
        $bytes[6] = chr((ord($bytes[6]) & 0x0f) | 0x40);
        $bytes[8] = chr((ord($bytes[8]) & 0x3f) | 0x80);

        return vsprintf('%s%s-%s-%s-%s-%s%s%s', str_split(bin2hex($bytes), 4));
    }

    private static function confirmationCode(): string
    {
        $code = '';
        $max = strlen(self::CODE_ALPHABET) - 1;
        for ($i = 0; $i < 8; $i++) {
            $code .= self::CODE_ALPHABET[random_int(0, $max)];
        }

        return $code;
    }
}
```

- [ ] **Step 3: Write `src/Domain/Reservations.php`**

In `confirmPayment`, the "hold expired and stations were taken" branch records the `expired` status and must let the transaction commit. It therefore returns the marker string `'hold_expired'` from the closure and throws only after the transaction has committed; throwing inside the closure would roll the status change back.

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

use OpenArcade\Db\Transaction;
use OpenArcade\Settings\SettingsRepository;
use OpenArcade\Support\Clock;
use PDO;

final class Reservations
{
    public function __construct(
        private PDO $pdo,
        private ReservationRepository $reservations,
        private HoursRepository $hours,
        private PriceRepository $prices,
        private StationRepository $stations,
        private SettingsRepository $settings,
        private Clock $clock,
    ) {
    }

    public static function build(PDO $pdo, Clock $clock): self
    {
        return new self(
            $pdo,
            new ReservationRepository($pdo),
            new HoursRepository($pdo),
            new PriceRepository($pdo),
            new StationRepository($pdo),
            new SettingsRepository($pdo),
            $clock,
        );
    }

    /** @throws BookingRejected */
    public function create(BookingRequest $request, BookingRules $rules): Reservation
    {
        $errors = $request->validate($rules->contactRequired);
        if ($errors !== []) {
            throw new BookingRejected('validation_failed', $errors);
        }

        $settings = $this->settings->load();
        $tz = $settings->tz();
        $now = $this->clock->now();
        $nowLocal = $now->setTimezone($tz);
        $today = $nowLocal->format('Y-m-d');

        if ($request->localDate < $today) {
            throw new BookingRejected('date_in_past');
        }
        if ($rules->enforceAdvanceLimit) {
            $lastDate = $nowLocal->modify("+{$settings->maxAdvanceDays} days")->format('Y-m-d');
            if ($request->localDate > $lastDate) {
                throw new BookingRejected('too_far_ahead');
            }
        }

        $endMinute = $request->startMinute + $request->durationMinutes;
        if ($endMinute > 1440) {
            throw new BookingRejected('outside_hours');
        }
        $hours = $this->hours->forDate($request->localDate);
        if ($rules->enforceHours) {
            if ($hours === null) {
                throw new BookingRejected('closed');
            }
            if ($request->startMinute < $hours->openMinute || $endMinute > $hours->closeMinute) {
                throw new BookingRejected('outside_hours');
            }
        }
        if ($rules->enforceGrid && $hours !== null
            && ($request->startMinute - $hours->openMinute) % $settings->slotStepMinutes !== 0) {
            throw new BookingRejected('off_grid');
        }
        if ($rules->enforceLeadTime && $request->localDate === $today) {
            $nowMinute = (int) $nowLocal->format('G') * 60 + (int) $nowLocal->format('i');
            if ($request->startMinute < $nowMinute + $settings->minLeadMinutes) {
                throw new BookingRejected('too_soon');
            }
        }

        $numbersById = $this->stations->activeNumbersById();
        if ($request->stationCount < 1 || $request->stationCount > count($numbersById)) {
            throw new BookingRejected('invalid_station_count');
        }

        if ($rules->complimentary) {
            $quote = new Quote(0, 0, 0, $settings->currency);
        } else {
            $weekday = (int) (new \DateTimeImmutable($request->localDate))->format('w');
            $priceList = $this->prices->load();
            if ($priceList->priceCents($weekday, $request->durationMinutes) === null) {
                throw new BookingRejected('duration_not_offered');
            }
            $quote = $priceList->quote($weekday, $request->durationMinutes, $request->stationCount, $settings->taxRateBp, $settings->currency);
        }

        // Wall-clock local time -> UTC. setTime() keeps this correct on daylight-saving change days.
        $utc = new \DateTimeZone('UTC');
        $midnight = new \DateTimeImmutable($request->localDate . ' 00:00:00', $tz);
        $startUtc = $midnight->setTime(intdiv($request->startMinute, 60), $request->startMinute % 60)->setTimezone($utc);
        $endUtc = $midnight->setTime(intdiv($endMinute, 60), $endMinute % 60)->setTimezone($utc);

        $held = $rules->requirePayment && $quote->totalCents > 0;
        $openMinute = $hours === null ? 0 : $hours->openMinute;

        $this->reservations->ensureDayRow($request->localDate);

        return Transaction::run($this->pdo, function () use ($request, $rules, $settings, $tz, $now, $numbersById, $quote, $startUtc, $endUtc, $endMinute, $held, $openMinute): Reservation {
            $this->reservations->lockDay($request->localDate);
            $blocks = $this->reservations->blocksForDate($request->localDate, $now, $tz);
            $free = Availability::freeStations(array_keys($numbersById), $blocks, $request->startMinute, $endMinute, $settings->bufferMinutes);
            $chosen = StationAllocator::choose($free, $blocks, $numbersById, $request->startMinute, $request->stationCount, $openMinute);
            if ($chosen === null) {
                throw new BookingRejected('slot_unavailable');
            }
            $timestamp = $now->format('Y-m-d H:i:s');
            $id = $this->reservations->insert([
                'status' => $held ? 'held' : 'confirmed',
                'first_name' => trim($request->firstName),
                'last_name' => trim($request->lastName),
                'email' => trim($request->email),
                'phone' => trim($request->phone),
                'comments' => $request->comments === null ? null : trim($request->comments),
                'local_date' => $request->localDate,
                'start_utc' => $startUtc->format('Y-m-d H:i:s'),
                'end_utc' => $endUtc->format('Y-m-d H:i:s'),
                'duration_minutes' => $request->durationMinutes,
                'station_count' => $request->stationCount,
                'subtotal_cents' => $quote->subtotalCents,
                'tax_cents' => $quote->taxCents,
                'total_cents' => $quote->totalCents,
                'currency' => $quote->currency,
                'hold_expires_at' => $held ? $now->modify("+{$settings->holdMinutes} minutes")->format('Y-m-d H:i:s') : null,
                'created_by' => $rules->createdBy,
                'created_at' => $timestamp,
                'updated_at' => $timestamp,
            ], $chosen);

            $reservation = $this->reservations->find($id, $tz);
            if ($reservation === null) {
                throw new \LogicException('Reservation vanished after insert.');
            }

            return $reservation;
        });
    }

    /**
     * Marks a held reservation paid. Safe to call twice with the same payment id.
     * If the hold expired and the stations were taken meanwhile, the reservation becomes "expired"
     * and BookingRejected('hold_expired') tells the caller to refund.
     *
     * @throws BookingRejected
     */
    public function confirmPayment(int $id, string $provider, string $paymentId): Reservation
    {
        $settings = $this->settings->load();
        $tz = $settings->tz();
        $existing = $this->reservations->find($id, $tz);
        if ($existing === null) {
            throw new BookingRejected('not_found');
        }
        $this->reservations->ensureDayRow($existing->localDate);

        $result = Transaction::run($this->pdo, function () use ($id, $provider, $paymentId, $settings, $tz, $existing): Reservation|string {
            $this->reservations->lockDay($existing->localDate);
            $now = $this->clock->now();
            $current = $this->reservations->find($id, $tz, true);
            if ($current === null) {
                throw new BookingRejected('not_found');
            }
            if ($current->status === 'confirmed' && $current->paymentId === $paymentId) {
                return $current;
            }
            if ($current->status !== 'held') {
                throw new BookingRejected('wrong_status');
            }
            $expired = $current->holdExpiresAtUtc !== null && $current->holdExpiresAtUtc <= $now->format('Y-m-d H:i:s');
            if ($expired) {
                $blocks = $this->reservations->blocksForDate($current->localDate, $now, $tz, $id);
                $free = Availability::freeStations(
                    $current->stationIds,
                    $blocks,
                    $current->startMinute,
                    $current->startMinute + $current->durationMinutes,
                    $settings->bufferMinutes
                );
                if (count($free) !== count($current->stationIds)) {
                    $this->reservations->transition($id, ['held'], 'expired', $now);

                    return 'hold_expired';
                }
            }
            $this->reservations->transition($id, ['held'], 'confirmed', $now, $provider, $paymentId);
            $confirmed = $this->reservations->find($id, $tz);
            if ($confirmed === null) {
                throw new \LogicException('Reservation vanished after confirm.');
            }

            return $confirmed;
        });

        if (is_string($result)) {
            throw new BookingRejected($result);
        }

        return $result;
    }

    public function failPayment(int $id): void
    {
        $this->reservations->transition($id, ['held'], 'payment_failed', $this->clock->now());
    }

    /** @throws BookingRejected */
    public function cancel(int $id): void
    {
        if (!$this->reservations->transition($id, ['held', 'confirmed'], 'cancelled', $this->clock->now())) {
            throw new BookingRejected('wrong_status');
        }
    }

    public function expireHolds(): int
    {
        return $this->reservations->expireHolds($this->clock->now());
    }
}
```

- [ ] **Step 4: Write the fixture `tests/Integration/VenueFixture.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\BookingRequest;
use OpenArcade\Domain\HoursRepository;
use OpenArcade\Domain\PriceRepository;
use OpenArcade\Domain\StationRepository;
use OpenArcade\Settings\SettingsRepository;
use PDO;

final class VenueFixture
{
    /** Chicago time, 9.35% tax, open 10:00-22:00 every day, 60 and 90 minute sessions, Saturday surcharge. */
    public static function seed(PDO $pdo, int $stations = 2): void
    {
        $settings = new SettingsRepository($pdo);
        $settings->set('timezone', 'America/Chicago');
        $settings->set('tax_rate_bp', '935');
        (new StationRepository($pdo))->syncCount($stations);
        $hours = new HoursRepository($pdo);
        for ($weekday = 0; $weekday <= 6; $weekday++) {
            $hours->setWeekday($weekday, 600, 1320, false);
        }
        $prices = new PriceRepository($pdo);
        $prices->set(-1, 60, 2500);
        $prices->set(-1, 90, 3550);
        $prices->set(6, 60, 3000);
    }

    public static function request(string $date, int $startMinute, int $duration = 60, int $stations = 1, string $email = 'alex.rivera@example.com'): BookingRequest
    {
        return new BookingRequest($date, $startMinute, $duration, $stations, 'Alex', 'Rivera', $email, '785-555-0142', null);
    }
}
```

- [ ] **Step 5: Write `tests/Integration/ReservationsTest.php`**

The fixed clock is Monday 2026-09-21 14:00 UTC, which is 09:00 in Chicago.

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\BookingRejected;
use OpenArcade\Domain\BookingRequest;
use OpenArcade\Domain\BookingRules;
use OpenArcade\Domain\HoursRepository;
use OpenArcade\Domain\Reservations;
use OpenArcade\Support\FixedClock;
use PDO;
use PHPUnit\Framework\TestCase;

final class ReservationsTest extends TestCase
{
    private PDO $pdo;
    private FixedClock $clock;
    private Reservations $service;

    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
        $this->pdo = TestDb::fresh();
        VenueFixture::seed($this->pdo, 2);
        $this->clock = new FixedClock('2026-09-21 14:00:00');
        $this->service = Reservations::build($this->pdo, $this->clock);
    }

    private function rejects(string $reason, BookingRequest $request, ?BookingRules $rules = null): BookingRejected
    {
        try {
            $this->service->create($request, $rules ?? BookingRules::customer(false));
        } catch (BookingRejected $rejected) {
            self::assertSame($reason, $rejected->reason);

            return $rejected;
        }
        self::fail("expected rejection {$reason}");
    }

    public function testCustomerBookingIsConfirmedPricedAndStoredInUtc(): void
    {
        $reservation = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(false));
        self::assertSame('confirmed', $reservation->status);
        self::assertSame(2500, $reservation->subtotalCents);
        self::assertSame(234, $reservation->taxCents);
        self::assertSame(2734, $reservation->totalCents);
        self::assertSame('2026-09-22 15:00:00', $reservation->startUtc);
        self::assertSame('2026-09-22 16:00:00', $reservation->endUtc);
        self::assertSame(600, $reservation->startMinute);
        self::assertCount(1, $reservation->stationIds);
        self::assertMatchesRegularExpression('/^[A-HJ-NP-Z2-9]{8}$/', $reservation->confirmationCode);
        self::assertNull($reservation->holdExpiresAtUtc);
    }

    public function testPaymentRequiredCreatesAHoldThatExpires(): void
    {
        $reservation = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        self::assertSame('held', $reservation->status);
        self::assertSame('2026-09-21 14:10:00', $reservation->holdExpiresAtUtc);
    }

    public function testSaturdayUsesTheWeekdayOverride(): void
    {
        $reservation = $this->service->create(VenueFixture::request('2026-09-26', 600), BookingRules::customer(false));
        self::assertSame(3000, $reservation->subtotalCents);
    }

    public function testSecondBookingGetsTheOtherStationAndThirdIsRefused(): void
    {
        $first = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(false));
        $second = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(false));
        self::assertNotSame($first->stationIds, $second->stationIds);
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 600));
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 630));
    }

    public function testBufferBlocksBackToBackButAllowsTheNextGridSlot(): void
    {
        $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 660));
        $later = $this->service->create(VenueFixture::request('2026-09-22', 690), BookingRules::customer(false));
        self::assertSame('confirmed', $later->status);
    }

    public function testCustomerRuleRejections(): void
    {
        $this->rejects('date_in_past', VenueFixture::request('2026-09-20', 600));
        $this->rejects('too_far_ahead', VenueFixture::request('2027-01-15', 600));
        $this->rejects('outside_hours', VenueFixture::request('2026-09-22', 540));
        $this->rejects('outside_hours', VenueFixture::request('2026-09-22', 1290, 60));
        $this->rejects('off_grid', VenueFixture::request('2026-09-22', 615));
        $this->rejects('duration_not_offered', VenueFixture::request('2026-09-22', 600, 45));
        $this->rejects('invalid_station_count', VenueFixture::request('2026-09-22', 600, 60, 3));
        $rejected = $this->rejects('validation_failed', VenueFixture::request('2026-09-22', 600, 60, 1, 'not-an-email'));
        self::assertArrayHasKey('email', $rejected->fieldErrors);
    }

    public function testTooSoonUsesLeadTimeOnTheSameDay(): void
    {
        // Now is 09:00 local, opening is 10:00, lead time 30 minutes: 10:00 is fine.
        $ok = $this->service->create(VenueFixture::request('2026-09-21', 600), BookingRules::customer(false));
        self::assertSame('confirmed', $ok->status);
        $this->clock->advanceMinutes(45); // 09:45 local; 10:00 is now inside the 30 minute lead time
        $this->rejects('too_soon', VenueFixture::request('2026-09-21', 600));
    }

    public function testClosedDayIsRefused(): void
    {
        (new HoursRepository($this->pdo))->addClosedDate('2026-09-23', 'Holiday');
        $this->rejects('closed', VenueFixture::request('2026-09-23', 600));
    }

    public function testAdminMayBookOffGridInsideLeadTimeAndComplimentaryButNeverOverlap(): void
    {
        $this->clock->advanceMinutes(75); // 10:15 local
        $walkIn = $this->service->create(
            new BookingRequest('2026-09-21', 617, 45, 2, 'Walk', 'In', '', '', 'paid at counter'),
            BookingRules::admin(true)
        );
        self::assertSame('confirmed', $walkIn->status);
        self::assertSame(0, $walkIn->totalCents);
        self::assertCount(2, $walkIn->stationIds);
        $this->rejects('slot_unavailable', new BookingRequest('2026-09-21', 640, 45, 1, 'Second', 'Group', '', ''), BookingRules::admin(true));
        $this->rejects('duration_not_offered', new BookingRequest('2026-09-21', 900, 45, 1, 'No', 'Price', '', ''), BookingRules::admin(false));
    }

    public function testExpiredHoldFreesTheStation(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 1);
        $clock = new FixedClock('2026-09-21 14:00:00');
        $service = Reservations::build($pdo, $clock);
        $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        try {
            $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
            self::fail('expected slot_unavailable while the hold is live');
        } catch (BookingRejected $rejected) {
            self::assertSame('slot_unavailable', $rejected->reason);
        }
        $clock->advanceMinutes(11);
        $second = $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        self::assertSame('held', $second->status);
        self::assertSame(1, $service->expireHolds());
    }

    public function testConfirmPaymentIsIdempotentAndFailAndCancelFreeTheStation(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $confirmed = $this->service->confirmPayment($held->id, 'square', 'pay_123');
        self::assertSame('confirmed', $confirmed->status);
        self::assertSame('pay_123', $confirmed->paymentId);
        self::assertSame('confirmed', $this->service->confirmPayment($held->id, 'square', 'pay_123')->status);

        $this->service->cancel($held->id);
        $again = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $this->service->failPayment($again->id);
        $third = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        self::assertSame('confirmed', $third->status);

        try {
            $this->service->cancel($again->id);
            self::fail('expected wrong_status');
        } catch (BookingRejected $rejected) {
            self::assertSame('wrong_status', $rejected->reason);
        }
    }

    public function testConfirmAfterExpiryWithStationsTakenExpiresTheHoldAndTellsCallerToRefund(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        try {
            $this->service->confirmPayment($held->id, 'square', 'pay_late');
            self::fail('expected hold_expired');
        } catch (BookingRejected $rejected) {
            self::assertSame('hold_expired', $rejected->reason);
        }
        $status = $this->pdo->query('SELECT status FROM reservations WHERE id = ' . $held->id);
        self::assertSame('expired', $status === false ? null : $status->fetchColumn());
    }

    public function testConfirmAfterExpiryStillWorksWhenStationsAreStillFree(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        self::assertSame('confirmed', $this->service->confirmPayment($held->id, 'square', 'pay_slow')->status);
    }

    public function testDaylightSavingChangeKeepsWallClockTime(): void
    {
        $clock = new FixedClock('2026-03-01 14:00:00');
        $service = Reservations::build($this->pdo, $clock);
        $before = $service->create(VenueFixture::request('2026-03-07', 600), BookingRules::customer(false));
        $after = $service->create(VenueFixture::request('2026-03-08', 600), BookingRules::customer(false));
        self::assertSame('2026-03-07 16:00:00', $before->startUtc, '10:00 CST');
        self::assertSame('2026-03-08 15:00:00', $after->startUtc, '10:00 CDT');
        self::assertSame(600, $after->startMinute);
    }
}
```

- [ ] **Step 6: Run the tests, then fix until green**

Run: `docker compose run --rm app composer test`
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Add reservation write path with per-date locking, holds and payment states"
```

---

### Task 12: Concurrency proof

Real operating-system processes race for the same stations. This is the test that proves the no-double-booking guarantee.

**Files:** Create `tests/Integration/workers/book_worker.php`, `tests/Integration/ConcurrencyTest.php`.

- [ ] **Step 1: Write `tests/Integration/workers/book_worker.php`**

```php
<?php

declare(strict_types=1);

use OpenArcade\Domain\BookingRejected;
use OpenArcade\Domain\BookingRules;
use OpenArcade\Domain\Reservations;
use OpenArcade\Support\FixedClock;
use OpenArcade\Tests\Integration\TestDb;
use OpenArcade\Tests\Integration\VenueFixture;

require dirname(__DIR__, 3) . '/vendor/autoload.php';

[, $startAt, $date, $startMinute, $stations] = $argv;

$service = Reservations::build(TestDb::connect(), new FixedClock('2026-09-21 14:00:00'));
while (microtime(true) < (float) $startAt) {
    usleep(500);
}
try {
    $reservation = $service->create(VenueFixture::request($date, (int) $startMinute, 60, (int) $stations), BookingRules::customer(false));
    echo json_encode(['result' => 'booked', 'stations' => $reservation->stationIds]);
} catch (BookingRejected $rejected) {
    echo json_encode(['result' => $rejected->reason]);
} catch (\Throwable $error) {
    echo json_encode(['result' => 'error', 'message' => $error->getMessage()]);
}
```

- [ ] **Step 2: Write `tests/Integration/ConcurrencyTest.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use PHPUnit\Framework\TestCase;

final class ConcurrencyTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    /** @return array<int, array<string, mixed>> decoded worker outputs */
    private function race(int $workers, string $date, int $startMinute, int $stationsEach): array
    {
        $startAt = sprintf('%.4F', microtime(true) + 1.5);
        $processes = [];
        for ($i = 0; $i < $workers; $i++) {
            $pipes = [];
            $process = proc_open(
                [PHP_BINARY, __DIR__ . '/workers/book_worker.php', $startAt, $date, (string) $startMinute, (string) $stationsEach],
                [1 => ['pipe', 'w'], 2 => ['pipe', 'w']],
                $pipes
            );
            self::assertIsResource($process);
            $processes[] = [$process, $pipes];
        }
        $results = [];
        foreach ($processes as [$process, $pipes]) {
            $out = stream_get_contents($pipes[1]);
            $err = stream_get_contents($pipes[2]);
            fclose($pipes[1]);
            fclose($pipes[2]);
            proc_close($process);
            $decoded = json_decode((string) $out, true);
            self::assertIsArray($decoded, 'worker output: ' . $out . ' stderr: ' . $err);
            $results[] = $decoded;
        }

        return $results;
    }

    public function testEightWritersForOneStationExactlyOneWins(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 1);
        $results = $this->race(8, '2026-09-22', 600, 1);
        $outcomes = array_count_values(array_column($results, 'result'));
        self::assertSame(
            ['booked' => 1, 'slot_unavailable' => 7],
            ['booked' => $outcomes['booked'] ?? 0, 'slot_unavailable' => $outcomes['slot_unavailable'] ?? 0],
            json_encode($results) ?: ''
        );
        $count = $pdo->query("SELECT COUNT(*) FROM reservations WHERE status = 'confirmed'");
        self::assertSame(1, $count === false ? -1 : (int) $count->fetchColumn());
    }

    public function testEightWritersForThreeStationsNeverShareAStation(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 3);
        $results = $this->race(8, '2026-09-22', 600, 1);
        $booked = array_values(array_filter($results, static fn (array $r): bool => $r['result'] === 'booked'));
        self::assertCount(3, $booked, json_encode($results) ?: '');
        $stations = array_merge(...array_column($booked, 'stations'));
        sort($stations);
        self::assertSame(array_values(array_unique($stations)), $stations, 'a station was given to two bookings');
        self::assertCount(3, $stations);
    }

    public function testGroupsWantingTwoOfThreeStationsOnlyOneFits(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 3);
        $results = $this->race(6, '2026-09-22', 600, 2);
        $outcomes = array_count_values(array_column($results, 'result'));
        self::assertSame(1, $outcomes['booked'] ?? 0, json_encode($results) ?: '');
        self::assertSame(5, $outcomes['slot_unavailable'] ?? 0, json_encode($results) ?: '');
    }
}
```

- [ ] **Step 3: Run it five times to shake out flakiness**

Run: `docker compose run --rm app sh -c 'for i in 1 2 3 4 5; do vendor/bin/phpunit tests/Integration/ConcurrencyTest.php || exit 1; done'`
Expected: five green runs. Any `error` result (deadlock, lock wait) is a bug in the locking code, not in the test: fix `Reservations` or `ReservationRepository`. Never loosen the assertions.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Prove no double booking with a multi-process race test"
```

---

### Task 13: Command-line tool

**Files:** Create `src/Console/Application.php`, `bin/console`, `tests/Integration/ConsoleTest.php`.

Commands: `migrate`, `install`, `admin:create`, `seed:demo`, `holds:release`, `help`. The admin password is read from the `--admin-password=` option, else from the `OPENARCADE_ADMIN_PASSWORD` environment variable (preferred for agents, keeps it out of shell history), else prompted.

Note for test authors: PHPUnit's `TestCase` already defines `run()` and `count()`. Do not name helpers that.

- [ ] **Step 1: Write the failing test `tests/Integration/ConsoleTest.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Console\Application;
use OpenArcade\Support\FixedClock;
use PDO;
use PHPUnit\Framework\TestCase;

final class ConsoleTest extends TestCase
{
    private PDO $pdo;
    /** @var string[] */
    private array $output = [];

    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
        $this->pdo = TestDb::fresh();
        $this->output = [];
    }

    /** @param string[] $args */
    private function console(array $args): int
    {
        $app = new Application(
            $this->pdo,
            dirname(__DIR__, 2) . '/migrations',
            new FixedClock('2026-09-21 14:00:00'),
            function (string $line): void {
                $this->output[] = $line;
            }
        );

        return $app->run(['console', ...$args]);
    }

    private function rows(string $table): int
    {
        $query = $this->pdo->query("SELECT COUNT(*) FROM {$table}");

        return $query === false ? -1 : (int) $query->fetchColumn();
    }

    public function testInstallSeedsDefaultsAndCreatesAdminOnce(): void
    {
        $args = ['install', '--admin-user=owner', '--admin-password=correct-horse-battery', '--stations=5', '--venue=Orbit VR', '--timezone=America/Denver'];
        self::assertSame(0, $this->console($args));
        self::assertSame(5, $this->rows('stations'));
        self::assertSame(7, $this->rows('business_hours'));
        self::assertSame(3, $this->rows('prices'));
        self::assertSame(1, $this->rows('admins'));
        $hash = $this->pdo->query("SELECT password_hash FROM admins WHERE username = 'owner'");
        self::assertTrue(password_verify('correct-horse-battery', $hash === false ? '' : (string) $hash->fetchColumn()));

        self::assertSame(0, $this->console($args), 'install is safe to run again');
        self::assertSame(1, $this->rows('admins'));
        self::assertSame(3, $this->rows('prices'));
    }

    public function testShortPasswordIsRefusedAndChangesNothing(): void
    {
        self::assertSame(1, $this->console(['install', '--admin-user=owner', '--admin-password=short']));
        self::assertSame(0, $this->rows('admins'));
        self::assertSame(0, $this->rows('stations'));
        self::assertStringContainsString('12 characters', implode("\n", $this->output));
    }

    public function testSeedDemoCreatesFakeReservationsOnly(): void
    {
        $this->console(['install', '--admin-user=owner', '--admin-password=correct-horse-battery']);
        self::assertSame(0, $this->console(['seed:demo']));
        self::assertGreaterThanOrEqual(8, $this->rows('reservations'));
        $real = $this->pdo->query("SELECT COUNT(*) FROM reservations WHERE email NOT LIKE '%@example.com'");
        self::assertSame(0, $real === false ? -1 : (int) $real->fetchColumn());
    }

    public function testUnknownCommandShowsHelpAndFails(): void
    {
        self::assertSame(1, $this->console(['nonsense']));
        self::assertStringContainsString('install', implode("\n", $this->output));
    }
}
```

- [ ] **Step 2: Write `src/Console/Application.php`**

```php
<?php

declare(strict_types=1);

namespace OpenArcade\Console;

use OpenArcade\Db\Migrator;
use OpenArcade\Domain\BookingRejected;
use OpenArcade\Domain\BookingRequest;
use OpenArcade\Domain\BookingRules;
use OpenArcade\Domain\HoursRepository;
use OpenArcade\Domain\PriceRepository;
use OpenArcade\Domain\Reservations;
use OpenArcade\Domain\StationRepository;
use OpenArcade\Settings\SettingsRepository;
use OpenArcade\Support\Clock;
use PDO;

final class Application
{
    private const DEMO_PEOPLE = [
        ['Alex', 'Rivera'], ['Sam', 'Okafor'], ['Jordan', 'Lee'], ['Taylor', 'Novak'], ['Morgan', 'Diaz'],
        ['Casey', 'Nguyen'], ['Riley', 'Patel'], ['Jamie', 'Kowalski'], ['Avery', 'Santos'], ['Quinn', 'Haddad'],
    ];

    /** @var callable(string): void */
    private $write;

    /** @param callable(string): void $write */
    public function __construct(private PDO $pdo, private string $migrationsDir, private Clock $clock, callable $write)
    {
        $this->write = $write;
    }

    /** @param string[] $argv */
    public function run(array $argv): int
    {
        $command = $argv[1] ?? 'help';
        $options = [];
        foreach (array_slice($argv, 2) as $arg) {
            if (preg_match('/^--([a-z][a-z0-9\-]*)(?:=(.*))?$/', $arg, $m) === 1) {
                $options[$m[1]] = $m[2] ?? '1';
            }
        }

        try {
            return match ($command) {
                'migrate' => $this->migrate(),
                'install' => $this->install($options),
                'admin:create' => $this->createAdmin($options),
                'seed:demo' => $this->seedDemo(),
                'holds:release' => $this->releaseHolds(),
                'help' => $this->help(0),
                default => $this->help(1),
            };
        } catch (\InvalidArgumentException $error) {
            ($this->write)('Error: ' . $error->getMessage());

            return 1;
        }
    }

    private function help(int $exitCode): int
    {
        foreach ([
            'Usage: php bin/console <command> [--option=value]',
            '  migrate        Apply database migrations',
            '  install        Migrate, seed defaults, create the first admin',
            '                 --admin-user= --admin-password= [--stations=4] [--venue=] [--timezone=]',
            '                 (the password may come from the OPENARCADE_ADMIN_PASSWORD environment variable)',
            '  admin:create   Create another admin: --admin-user= --admin-password=',
            '  seed:demo      Add fake reservations for demos and screenshots',
            '  holds:release  Expire unpaid holds',
        ] as $line) {
            ($this->write)($line);
        }

        return $exitCode;
    }

    private function migrate(): int
    {
        $applied = (new Migrator($this->pdo, $this->migrationsDir))->migrate();
        ($this->write)($applied === [] ? 'Database is up to date.' : 'Applied: ' . implode(', ', $applied));

        return 0;
    }

    /** @param array<string,string> $options */
    private function install(array $options): int
    {
        // Validate the admin first so a bad password changes nothing.
        $username = $this->adminUsername($options);
        $password = $this->adminPassword($options);

        $this->migrate();

        $settings = new SettingsRepository($this->pdo);
        if (isset($options['venue'])) {
            $settings->set('venue_name', $options['venue']);
        }
        if (isset($options['timezone'])) {
            $settings->set('timezone', $options['timezone']);
        }

        $stations = new StationRepository($this->pdo);
        if ($stations->activeNumbersById() === [] || isset($options['stations'])) {
            $stations->syncCount((int) ($options['stations'] ?? 4));
        }

        if ($this->isEmpty('business_hours')) {
            $hours = new HoursRepository($this->pdo);
            for ($weekday = 0; $weekday <= 6; $weekday++) {
                $hours->setWeekday($weekday, 600, 1320, false);
            }
        }
        if ($this->isEmpty('prices')) {
            $prices = new PriceRepository($this->pdo);
            $prices->set(-1, 60, 2500);
            $prices->set(-1, 90, 3500);
            $prices->set(-1, 120, 4500);
        }

        $this->insertAdmin($username, $password);
        ($this->write)('Installed. Change hours, prices and stations in the dashboard settings.');

        return 0;
    }

    /** @param array<string,string> $options */
    private function createAdmin(array $options): int
    {
        $this->insertAdmin($this->adminUsername($options), $this->adminPassword($options));

        return 0;
    }

    private function seedDemo(): int
    {
        $service = Reservations::build($this->pdo, $this->clock);
        $tz = (new SettingsRepository($this->pdo))->load()->tz();
        $today = $this->clock->now()->setTimezone($tz);
        $created = 0;
        foreach (self::DEMO_PEOPLE as $index => [$first, $last]) {
            $request = new BookingRequest(
                $today->modify('+' . (1 + $index % 3) . ' days')->format('Y-m-d'),
                600 + ($index % 5) * 90,
                60,
                1 + $index % 2,
                $first,
                $last,
                strtolower($first . '.' . $last) . '@example.com',
                sprintf('785-555-01%02d', $index + 10),
                'Demo booking',
            );
            try {
                $service->create($request, BookingRules::admin());
                $created++;
            } catch (BookingRejected) {
                continue;
            }
        }
        ($this->write)("Created {$created} demo reservations.");

        return 0;
    }

    private function releaseHolds(): int
    {
        $count = Reservations::build($this->pdo, $this->clock)->expireHolds();
        ($this->write)("Expired {$count} unpaid hold(s).");

        return 0;
    }

    private function isEmpty(string $table): bool
    {
        $query = $this->pdo->query("SELECT COUNT(*) FROM {$table}");

        return $query === false || (int) $query->fetchColumn() === 0;
    }

    /** @param array<string,string> $options */
    private function adminUsername(array $options): string
    {
        $username = trim($options['admin-user'] ?? '');
        if (preg_match('/^[A-Za-z0-9_.\-]{3,50}$/', $username) !== 1) {
            throw new \InvalidArgumentException('--admin-user must be 3-50 letters, digits, dot, dash or underscore.');
        }

        return $username;
    }

    /** @param array<string,string> $options */
    private function adminPassword(array $options): string
    {
        $password = $options['admin-password'] ?? (getenv('OPENARCADE_ADMIN_PASSWORD') ?: '');
        if ($password === '' && defined('STDIN') && function_exists('posix_isatty') && posix_isatty(STDIN)) {
            ($this->write)('Admin password (12+ characters):');
            $password = trim((string) fgets(STDIN));
        }
        if (strlen($password) < 12) {
            throw new \InvalidArgumentException('The admin password must be at least 12 characters.');
        }

        return $password;
    }

    private function insertAdmin(string $username, string $password): void
    {
        $exists = $this->pdo->prepare('SELECT 1 FROM admins WHERE username = ?');
        $exists->execute([$username]);
        if ($exists->fetchColumn() !== false) {
            ($this->write)("Admin {$username} already exists; left unchanged.");

            return;
        }
        $this->pdo->prepare('INSERT INTO admins (username, password_hash, created_at) VALUES (?, ?, ?)')
            ->execute([$username, password_hash($password, PASSWORD_DEFAULT), $this->clock->now()->format('Y-m-d H:i:s')]);
        ($this->write)("Admin {$username} created.");
    }
}
```

- [ ] **Step 3: Write `bin/console`**

```php
#!/usr/bin/env php
<?php

declare(strict_types=1);

require __DIR__ . '/../vendor/autoload.php';

use OpenArcade\Console\Application;
use OpenArcade\Db\Connection;
use OpenArcade\Support\Config;
use OpenArcade\Support\SystemClock;

$root = dirname(__DIR__);
try {
    $pdo = Connection::fromConfig(Config::load($root));
} catch (\Throwable $error) {
    fwrite(STDERR, 'Cannot connect to the database. Check DB_* in .env. ' . $error->getMessage() . "\n");
    exit(2);
}

exit((new Application($pdo, $root . '/migrations', new SystemClock(), static function (string $line): void {
    echo $line . "\n";
}))->run($argv));
```

- [ ] **Step 4: Run the tests, then try it for real**

Run: `docker compose run --rm app composer test`
Expected: all pass.

Run: `docker compose run --rm -e OPENARCADE_ADMIN_PASSWORD=local-dev-password-123 app php bin/console install --admin-user=owner --stations=4` then `docker compose run --rm app php bin/console seed:demo`
Expected: `Installed...` then `Created 10 demo reservations.`

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Add console: migrate, install, admin:create, seed:demo, holds:release"
```

---

### Task 14: Continuous integration and the full gate

**Files:** Create `.github/workflows/ci.yml`. Modify `README.md`.

- [ ] **Step 1: Write `.github/workflows/ci.yml`**

```yaml
name: CI
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    services:
      db:
        image: mariadb:10.11
        env:
          MARIADB_ROOT_PASSWORD: root
          MARIADB_DATABASE: openarcade_test
          MARIADB_USER: openarcade
          MARIADB_PASSWORD: openarcade
        ports:
          - 3306:3306
        options: >-
          --health-cmd="healthcheck.sh --connect --innodb_initialized"
          --health-interval=5s --health-timeout=5s --health-retries=20
    env:
      DB_HOST: 127.0.0.1
      DB_PORT: "3306"
      DB_USER: openarcade
      DB_PASSWORD: openarcade
      TEST_DB_NAME: openarcade_test
    steps:
      - uses: actions/checkout@v4
      - uses: shivammathur/setup-php@v2
        with:
          php-version: "8.2"
          extensions: pdo_mysql, mbstring
          coverage: none
      - run: composer install --no-interaction --prefer-dist
      - run: composer cs
      - run: composer stan
      - run: composer test
      - run: composer clean-check

  secrets:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - uses: gitleaks/gitleaks-action@v2
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

- [ ] **Step 2: Run the whole gate and fix everything it reports**

Run: `docker compose run --rm app composer cs:fix` then `docker compose run --rm app composer check`
Expected: coding standard clean, PHPStan `[OK] No errors`, all tests green, `check-clean: OK`.

- [ ] **Step 3: Replace the Development section of `README.md`**

```markdown
## Development

    docker compose build
    docker compose run --rm app composer install
    docker compose run --rm app composer check      # style, static analysis, tests, clean-repo gate

    # try it
    docker compose run --rm -e OPENARCADE_ADMIN_PASSWORD=local-dev-password-123 app php bin/console install --admin-user=owner
    docker compose run --rm app php bin/console seed:demo

`composer check` must pass before every commit. The clean-repo gate (`bin/check-clean`) fails on API keys, real email addresses and phone numbers. Test data uses `@example.com` and `555-01xx` only.
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Add CI workflow and document the quality gate"
```

---

## Self-review against the spec

| Spec section | Covered by |
| --- | --- |
| 4 Architecture (Support, Db, Domain, Console) | Tasks 3-13. Http, Auth, Payments, Realtime, Mail are Plans 2-3. |
| 5 Configuration (`.env`, settings in the database) | Tasks 3 and 6. Payment, real-time and mail keys arrive with their plans. |
| 6 Data model | Task 5 (every table, including those first used by later plans). |
| 7 Availability and the no-double-booking guarantee | Tasks 9, 10, 11, 12. |
| 8 Payment states (`held`, confirm, fail, expiry, late confirm) | Task 11. The Square call itself is Plan 3. |
| 14 Install and console | Task 13. `doctor`, the setup page and the agent kit are Plan 5. |
| 15 Tests, static analysis, clean-repo gate, CI | Tasks 2, 12, 14. |
| Not in this plan | Spec sections 9-13 and the rest of 14: Plans 2-5. |
