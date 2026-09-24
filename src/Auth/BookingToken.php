<?php

declare(strict_types=1);

namespace OpenArcade\Auth;

use OpenArcade\Support\Clock;

/**
 * Proof that a booking request comes from a browser that first loaded the booking page: a random
 * nonce and its issue time, signed with the application key. It is stateless on purpose. The booking
 * page keeps no session and sets no cookie, so it works inside an iframe on another website in every
 * browser, and a cross-site page cannot obtain a token because the API sets no CORS headers.
 * Duplicate submissions are handled by the reservation's idempotency key, not by the token.
 */
final class BookingToken
{
    public const TTL_SECONDS = 4 * 3600;

    public function __construct(private string $appKey, private Clock $clock)
    {
    }

    public function issue(): string
    {
        $issuedAt = (string) $this->clock->now()->getTimestamp();
        $nonce = bin2hex(random_bytes(16));

        return $issuedAt . '.' . $nonce . '.' . $this->sign($issuedAt, $nonce);
    }

    public function verify(?string $token): bool
    {
        if (!is_string($token) || preg_match('/^(\d{1,12})\.([a-f0-9]{32})\.([a-f0-9]{64})$/', $token, $m) !== 1) {
            return false;
        }
        if (!hash_equals($this->sign($m[1], $m[2]), $m[3])) {
            return false;
        }
        $age = $this->clock->now()->getTimestamp() - (int) $m[1];

        return $age >= -60 && $age <= self::TTL_SECONDS;
    }

    private function sign(string $issuedAt, string $nonce): string
    {
        return hash_hmac('sha256', 'booking:' . $issuedAt . ':' . $nonce, $this->appKey);
    }
}
