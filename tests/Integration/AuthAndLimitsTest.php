<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Integration;

use ArcadeOS\Auth\AdminAuth;
use ArcadeOS\Auth\LoginThrottle;
use ArcadeOS\Http\ArraySession;
use ArcadeOS\Http\RateLimiter;
use ArcadeOS\Support\FixedClock;
use PDO;
use PHPUnit\Framework\TestCase;

final class AuthAndLimitsTest extends TestCase
{
    private PDO $pdo;
    private FixedClock $clock;

    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
        $this->pdo = TestDb::fresh();
        $this->clock = new FixedClock('2026-09-21 14:00:00');
        $this->pdo->prepare('INSERT INTO admins (username, password_hash, created_at) VALUES (?, ?, ?)')
            ->execute(['owner', password_hash('correct-horse-battery', PASSWORD_DEFAULT), '2026-09-01 00:00:00']);
    }

    public function testRateLimiterCountsPerWindowAndClient(): void
    {
        $limiter = new RateLimiter($this->pdo);
        for ($i = 0; $i < 3; $i++) {
            self::assertTrue($limiter->allow('b', 'client-a', 3, 60, $this->clock->now()));
        }
        self::assertFalse($limiter->allow('b', 'client-a', 3, 60, $this->clock->now()));
        self::assertTrue($limiter->allow('b', 'client-b', 3, 60, $this->clock->now()));
        self::assertTrue($limiter->allow('other', 'client-a', 3, 60, $this->clock->now()));
        $this->clock->advanceMinutes(1);
        self::assertTrue($limiter->allow('b', 'client-a', 3, 60, $this->clock->now()), 'new window');
    }

    public function testLoginThrottleBlocksByUsernameAndByClient(): void
    {
        $throttle = new LoginThrottle($this->pdo);
        $now = $this->clock->now();
        for ($i = 0; $i < 5; $i++) {
            $throttle->record('owner', 'client-a', false, $now);
        }
        self::assertTrue($throttle->isBlocked('owner', 'client-a', $now), 'the guessing client is blocked for that username');
        self::assertFalse($throttle->isBlocked('owner', 'client-z', $now), 'the real owner elsewhere is not locked out');
        self::assertFalse($throttle->isBlocked('someone-else', 'client-a', $now));
        for ($i = 0; $i < 15; $i++) {
            $throttle->record('user' . $i, 'client-a', false, $now);
        }
        self::assertTrue($throttle->isBlocked('fresh-user', 'client-a', $now), 'client blocked after 20 failures');
        self::assertFalse($throttle->isBlocked('owner', 'client-a', $now->modify('+16 minutes')));

        for ($i = 0; $i < 100; $i++) {
            $throttle->record('owner', 'bot-' . $i, false, $now);
        }
        self::assertTrue($throttle->isBlocked('owner', 'client-z', $now), 'a distributed guess is stopped');
        self::assertSame(105, $throttle->clear('owner'));
        self::assertFalse($throttle->isBlocked('owner', 'client-z', $now), 'admin:unlock clears it');
    }

    public function testAdminAuthLoginCurrentIdleAndLogout(): void
    {
        $auth = new AdminAuth($this->pdo, $this->clock, 30);
        $session = new ArraySession();
        self::assertNull($auth->login($session, 'owner', 'wrong'));
        self::assertNull($auth->login($session, 'nobody', 'correct-horse-battery'));
        self::assertNull($auth->current($session));

        $user = $auth->login($session, 'owner', 'correct-horse-battery');
        self::assertNotNull($user);
        self::assertSame('owner', $user->username);
        self::assertSame(1, $session->regenerations);
        self::assertSame('owner', $auth->current($session)?->username);

        $this->clock->advanceMinutes(29);
        self::assertNotNull($auth->current($session), 'activity keeps the session alive');
        $this->clock->advanceMinutes(29);
        self::assertNotNull($auth->current($session));
        $this->clock->advanceMinutes(31);
        self::assertNull($auth->current($session), 'idle timeout');
        self::assertSame(1, $session->destructions);

        $auth->login($session, 'owner', 'correct-horse-battery');
        $auth->logout($session);
        self::assertNull($auth->current($session));
    }
}
