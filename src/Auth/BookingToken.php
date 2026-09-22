<?php

declare(strict_types=1);

namespace ArcadeOS\Auth;

use ArcadeOS\Http\Session;

/**
 * Proof that a booking request comes from a browser that first loaded the booking page:
 * a random nonce kept in the session, signed with the application key. It is rotated after
 * every accepted booking so a captured token cannot be replayed.
 */
final class BookingToken
{
    private const KEY = 'booking_nonce';

    public function __construct(private string $appKey)
    {
    }

    public function issue(Session $session): string
    {
        $nonce = $session->get(self::KEY);
        if (!is_string($nonce) || strlen($nonce) !== 32) {
            $nonce = bin2hex(random_bytes(16));
            $session->set(self::KEY, $nonce);
        }

        return $nonce . '.' . $this->sign($nonce);
    }

    public function verify(Session $session, ?string $token): bool
    {
        if (!is_string($token) || preg_match('/^([a-f0-9]{32})\.([a-f0-9]{64})$/', $token, $m) !== 1) {
            return false;
        }
        $nonce = $session->get(self::KEY);

        return is_string($nonce)
            && hash_equals($nonce, $m[1])
            && hash_equals($this->sign($m[1]), $m[2]);
    }

    public function rotate(Session $session): void
    {
        $session->remove(self::KEY);
    }

    private function sign(string $nonce): string
    {
        return hash_hmac('sha256', 'booking:' . $nonce, $this->appKey);
    }
}
