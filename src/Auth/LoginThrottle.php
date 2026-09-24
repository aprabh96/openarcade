<?php

declare(strict_types=1);

namespace OpenArcade\Auth;

use PDO;

/**
 * Slows password guessing without letting a stranger lock the owner out. Three counters over a
 * 15 minute window: a client that fails for one username is blocked for that username after 5
 * tries; a client that fails for any username is blocked after 20; and a username that fails from
 * anywhere is blocked after 100, which stops a distributed guess while staying out of reach of a
 * single nuisance. `bin/console admin:unlock` clears the counters for a username.
 */
final class LoginThrottle
{
    public const WINDOW_MINUTES = 15;
    public const MAX_PER_USERNAME_AND_CLIENT = 5;
    public const MAX_PER_CLIENT = 20;
    public const MAX_PER_USERNAME = 100;

    public function __construct(private PDO $pdo)
    {
    }

    public function isBlocked(string $username, string $ipHash, \DateTimeImmutable $nowUtc): bool
    {
        $since = $nowUtc->modify('-' . self::WINDOW_MINUTES . ' minutes')->format('Y-m-d H:i:s');

        $pair = $this->pdo->prepare('SELECT COUNT(*) FROM login_attempts WHERE username = ? AND ip_hash = ? AND succeeded = 0 AND created_at > ?');
        $pair->execute([$username, $ipHash, $since]);
        if ((int) $pair->fetchColumn() >= self::MAX_PER_USERNAME_AND_CLIENT) {
            return true;
        }

        $byClient = $this->pdo->prepare('SELECT COUNT(*) FROM login_attempts WHERE ip_hash = ? AND succeeded = 0 AND created_at > ?');
        $byClient->execute([$ipHash, $since]);
        if ((int) $byClient->fetchColumn() >= self::MAX_PER_CLIENT) {
            return true;
        }

        $byUser = $this->pdo->prepare('SELECT COUNT(*) FROM login_attempts WHERE username = ? AND succeeded = 0 AND created_at > ?');
        $byUser->execute([$username, $since]);

        return (int) $byUser->fetchColumn() >= self::MAX_PER_USERNAME;
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

    /** Forgets every failed attempt for a username. Returns the number of rows removed. */
    public function clear(string $username): int
    {
        $statement = $this->pdo->prepare('DELETE FROM login_attempts WHERE username = ? AND succeeded = 0');
        $statement->execute([mb_substr($username, 0, 50)]);

        return $statement->rowCount();
    }
}
