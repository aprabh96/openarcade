<?php

declare(strict_types=1);

namespace OpenArcade\Http;

/** PHP's own session, started lazily with secure cookie settings. */
final class NativeSession implements Session
{
    public const COOKIE = 'openarcade_session';

    private bool $started = false;

    public function __construct(private bool $secureCookie)
    {
    }

    public function get(string $key, mixed $default = null): mixed
    {
        $this->start();

        return $_SESSION[$key] ?? $default;
    }

    public function set(string $key, mixed $value): void
    {
        $this->start();
        $_SESSION[$key] = $value;
    }

    public function remove(string $key): void
    {
        $this->start();
        unset($_SESSION[$key]);
    }

    public function regenerate(): void
    {
        $this->start();
        session_regenerate_id(true);
    }

    public function destroy(): void
    {
        $this->start();
        $_SESSION = [];
        if (PHP_SAPI !== 'cli') {
            setcookie(self::COOKIE, '', [
                'expires' => time() - 3600,
                'path' => '/',
                'secure' => $this->secureCookie,
                'httponly' => true,
                'samesite' => 'Lax',
            ]);
        }
        session_destroy();
        $this->started = false;
    }

    private function start(): void
    {
        if ($this->started || session_status() === PHP_SESSION_ACTIVE) {
            $this->started = true;

            return;
        }
        session_name(self::COOKIE);
        session_set_cookie_params([
            'lifetime' => 0,
            'path' => '/',
            'secure' => $this->secureCookie,
            'httponly' => true,
            'samesite' => 'Lax',
        ]);
        ini_set('session.use_strict_mode', '1');
        ini_set('session.use_only_cookies', '1');
        session_start();
        $this->started = true;
    }
}
