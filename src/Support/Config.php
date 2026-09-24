<?php

declare(strict_types=1);

namespace ArcadeOS\Support;

final class Config
{
    public function __construct(private Env $env, private string $rootDir)
    {
    }

    public static function load(string $rootDir): self
    {
        return new self(Env::fromFile($rootDir . '/.env'), $rootDir);
    }

    public function env(): Env
    {
        return $this->env;
    }

    public function rootDir(): string
    {
        return $this->rootDir;
    }

    public function appEnv(): string
    {
        return $this->env->get('APP_ENV', 'production') ?? 'production';
    }

    public function debug(): bool
    {
        return $this->env->bool('APP_DEBUG', false);
    }

    public function appKey(): string
    {
        $key = $this->env->require('APP_KEY');
        if (strlen($key) < 32) {
            throw new \RuntimeException('APP_KEY must be at least 32 characters.');
        }

        return $key;
    }

    public function appUrl(): string
    {
        return rtrim($this->env->get('APP_URL', 'http://localhost') ?? 'http://localhost', '/');
    }

    /** @return array{host:string,port:int,name:string,user:string,password:string} */
    public function database(): array
    {
        return [
            'host' => $this->env->get('DB_HOST', 'localhost') ?? 'localhost',
            'port' => $this->env->int('DB_PORT', 3306),
            'name' => $this->env->require('DB_NAME'),
            'user' => $this->env->require('DB_USER'),
            'password' => $this->env->get('DB_PASSWORD', '') ?? '',
        ];
    }

    /** "none" (pay at the venue) or "square". */
    public function paymentMode(): string
    {
        return $this->oneOf('PAYMENT_MODE', ['none', 'square'], 'none');
    }

    /** @return array{environment:string,accessToken:string,applicationId:string,locationId:string} */
    public function square(): array
    {
        return [
            'environment' => $this->oneOf('SQUARE_ENV', ['sandbox', 'production'], 'sandbox'),
            'accessToken' => $this->env->require('SQUARE_ACCESS_TOKEN'),
            'applicationId' => $this->env->require('SQUARE_APPLICATION_ID'),
            'locationId' => $this->env->require('SQUARE_LOCATION_ID'),
        ];
    }

    /** "log", "mail" or "smtp". */
    public function mailDriver(): string
    {
        return $this->oneOf('MAIL_DRIVER', ['log', 'mail', 'smtp'], 'log');
    }

    /** @return array{host:string,port:int,user:string,password:string,encryption:string} */
    public function smtp(): array
    {
        return [
            'host' => $this->env->require('SMTP_HOST'),
            'port' => $this->env->int('SMTP_PORT', 587),
            'user' => $this->env->get('SMTP_USER', '') ?? '',
            'password' => $this->env->get('SMTP_PASSWORD', '') ?? '',
            'encryption' => $this->oneOf('SMTP_ENCRYPTION', ['tls', 'ssl', 'none'], 'tls'),
        ];
    }

    public function mailFromAddress(): string
    {
        return $this->env->get('MAIL_FROM_ADDRESS', '') ?? '';
    }

    public function mailFromName(): string
    {
        return $this->env->get('MAIL_FROM_NAME', 'Reservations') ?? 'Reservations';
    }

    /**
     * Origins allowed to embed the booking page in a frame, from EMBED_ALLOWED_ORIGINS (comma separated).
     *
     * @return string[]
     */
    public function embedAllowedOrigins(): array
    {
        $raw = $this->env->get('EMBED_ALLOWED_ORIGINS', '') ?? '';
        $origins = [];
        foreach (explode(',', $raw) as $origin) {
            $origin = trim($origin);
            if ($origin === '') {
                continue;
            }
            if (preg_match('#^https?://[a-z0-9.\-]+(:\d+)?$#i', $origin) !== 1) {
                throw new \RuntimeException("EMBED_ALLOWED_ORIGINS entry '{$origin}' must look like https://example.com");
            }
            $origins[] = rtrim($origin, '/');
        }

        return $origins;
    }

    public function setupToken(): ?string
    {
        return $this->env->get('SETUP_TOKEN');
    }

    /** Whether X-Forwarded-For / X-Forwarded-Proto from the immediate upstream may be trusted. */
    public function trustProxy(): bool
    {
        return $this->env->bool('TRUST_PROXY', false);
    }

    public function sessionIdleMinutes(): int
    {
        return max(5, $this->env->int('SESSION_IDLE_MINUTES', 480));
    }

    /** @param string[] $allowed */
    private function oneOf(string $key, array $allowed, string $default): string
    {
        $value = strtolower($this->env->get($key, $default) ?? $default);
        if (!in_array($value, $allowed, true)) {
            throw new \RuntimeException("{$key} must be one of: " . implode(', ', $allowed) . '.');
        }

        return $value;
    }
}
