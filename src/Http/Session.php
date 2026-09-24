<?php

declare(strict_types=1);

namespace OpenArcade\Http;

interface Session
{
    public function get(string $key, mixed $default = null): mixed;

    public function set(string $key, mixed $value): void;

    public function remove(string $key): void;

    /** New session id, same data. Call after login. */
    public function regenerate(): void;

    /** Forget everything and end the session. */
    public function destroy(): void;
}
