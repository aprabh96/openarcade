<?php

declare(strict_types=1);

namespace OpenArcade\Auth;

use OpenArcade\Http\Session;

/** Per-session token that every admin write must echo in the X-CSRF-Token header. */
final class Csrf
{
    private const KEY = 'csrf_token';

    public static function token(Session $session): string
    {
        $token = $session->get(self::KEY);
        if (!is_string($token) || strlen($token) !== 64) {
            $token = bin2hex(random_bytes(32));
            $session->set(self::KEY, $token);
        }

        return $token;
    }

    public static function verify(Session $session, ?string $provided): bool
    {
        $expected = $session->get(self::KEY);

        return is_string($expected) && is_string($provided) && $provided !== '' && hash_equals($expected, $provided);
    }

    public static function rotate(Session $session): string
    {
        $session->remove(self::KEY);

        return self::token($session);
    }
}
