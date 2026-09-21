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
}
