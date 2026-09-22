<?php

declare(strict_types=1);

namespace ArcadeOS\Auth;

use PDO;

/** Slows password guessing: a username or a client that fails too often must wait. */
final class LoginThrottle
{
    public const WINDOW_MINUTES = 15;
    public const MAX_PER_USERNAME = 5;
    public const MAX_PER_CLIENT = 20;

    public function __construct(private PDO $pdo)
    {
    }

    public function isBlocked(string $username, string $ipHash, \DateTimeImmutable $nowUtc): bool
    {
        $since = $nowUtc->modify('-' . self::WINDOW_MINUTES . ' minutes')->format('Y-m-d H:i:s');

        $byUser = $this->pdo->prepare('SELECT COUNT(*) FROM login_attempts WHERE username = ? AND succeeded = 0 AND created_at > ?');
        $byUser->execute([$username, $since]);
        if ((int) $byUser->fetchColumn() >= self::MAX_PER_USERNAME) {
            return true;
        }

        $byClient = $this->pdo->prepare('SELECT COUNT(*) FROM login_attempts WHERE ip_hash = ? AND succeeded = 0 AND created_at > ?');
        $byClient->execute([$ipHash, $since]);

        return (int) $byClient->fetchColumn() >= self::MAX_PER_CLIENT;
    }

    public function record(string $username, string $ipHash, bool $succeeded, \DateTimeImmutable $nowUtc): void
    {
        $this->pdo->prepare('INSERT INTO login_attempts (username, ip_hash, succeeded, created_at) VALUES (?, ?, ?, ?)')
            ->execute([mb_substr($username, 0, 50), $ipHash, $succeeded ? 1 : 0, $nowUtc->format('Y-m-d H:i:s')]);
        if (random_int(1, 50) === 1) {
            $this->pdo->prepare('DELETE FROM login_attempts WHERE created_at < ?')
                ->execute([$nowUtc->modify('-1 day')->format('Y-m-d H:i:s')]);
        }
    }
}
