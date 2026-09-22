<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

/** In-memory session for tests and the console. */
final class ArraySession implements Session
{
    /** @var array<string,mixed> */
    private array $data = [];

    public int $regenerations = 0;

    public int $destructions = 0;

    public function get(string $key, mixed $default = null): mixed
    {
        return $this->data[$key] ?? $default;
    }

    public function set(string $key, mixed $value): void
    {
        $this->data[$key] = $value;
    }

    public function remove(string $key): void
    {
        unset($this->data[$key]);
    }

    public function regenerate(): void
    {
        $this->regenerations++;
    }

    public function destroy(): void
    {
        $this->data = [];
        $this->destructions++;
    }
}
