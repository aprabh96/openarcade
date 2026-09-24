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
