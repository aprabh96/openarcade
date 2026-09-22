<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

use PDO;

/** Fixed-window counter per (bucket, client) in the rate_limits table. Works across PHP processes and servers. */
final class RateLimiter
{
    public function __construct(private PDO $pdo)
    {
    }

    /** Records one hit and reports whether the client is still within $limit hits per $windowSeconds. */
    public function allow(string $bucket, string $ipHash, int $limit, int $windowSeconds, \DateTimeImmutable $nowUtc): bool
    {
        $windowStart = gmdate('Y-m-d H:i:s', intdiv($nowUtc->getTimestamp(), $windowSeconds) * $windowSeconds);
        $this->pdo->prepare(
            'INSERT INTO rate_limits (bucket, ip_hash, window_start, hits) VALUES (?, ?, ?, 1) '
            . 'ON DUPLICATE KEY UPDATE hits = hits + 1'
        )->execute([$bucket, $ipHash, $windowStart]);

        $statement = $this->pdo->prepare('SELECT hits FROM rate_limits WHERE bucket = ? AND ip_hash = ? AND window_start = ?');
        $statement->execute([$bucket, $ipHash, $windowStart]);
        $hits = (int) $statement->fetchColumn();

        if (random_int(1, 100) === 1) {
            $this->pdo->prepare('DELETE FROM rate_limits WHERE window_start < ?')
                ->execute([$nowUtc->modify('-1 day')->format('Y-m-d H:i:s')]);
        }

        return $hits <= $limit;
    }
}
