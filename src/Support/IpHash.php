<?php

declare(strict_types=1);

namespace OpenArcade\Support;

/** IP addresses are never stored in clear; only a keyed hash, so logs and tables cannot be joined back to people. */
final class IpHash
{
    public static function of(string $ip, string $appKey): string
    {
        return hash_hmac('sha256', $ip, $appKey);
    }
}
