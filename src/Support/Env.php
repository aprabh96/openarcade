<?php

declare(strict_types=1);

namespace ArcadeOS\Support;

final class Env
{
    /** @param array<string,string> $fileValues */
    public function __construct(private array $fileValues = [], private bool $useRealEnvironment = true)
    {
    }

    public static function fromFile(?string $path): self
    {
        if ($path === null || !is_file($path) || !is_readable($path)) {
            return new self([]);
        }

        return new self(self::parse((string) file_get_contents($path)));
    }

    /** @return array<string,string> */
    public static function parse(string $contents): array
    {
        $values = [];
        foreach (preg_split('/\R/', $contents) ?: [] as $line) {
            $line = trim($line);
            if ($line === '' || $line[0] === '#') {
                continue;
            }
            $pos = strpos($line, '=');
            if ($pos === false) {
                continue;
            }
            $key = trim(substr($line, 0, $pos));
            if (preg_match('/^[A-Z][A-Z0-9_]*$/', $key) !== 1) {
                continue;
            }
            $value = trim(substr($line, $pos + 1));
            $length = strlen($value);
            $quoted = $length >= 2
                && (($value[0] === '"' && $value[$length - 1] === '"') || ($value[0] === "'" && $value[$length - 1] === "'"));
            if ($quoted) {
                $value = substr($value, 1, -1);
            } else {
                $hash = strpos($value, ' #');
                if ($hash !== false) {
                    $value = rtrim(substr($value, 0, $hash));
                }
            }
            $values[$key] = $value;
        }

        return $values;
    }

    public function get(string $key, ?string $default = null): ?string
    {
        if ($this->useRealEnvironment) {
            $real = getenv($key);
            if ($real !== false && $real !== '') {
                return $real;
            }
        }
        if (isset($this->fileValues[$key]) && $this->fileValues[$key] !== '') {
            return $this->fileValues[$key];
        }

        return $default;
    }

    public function require(string $key): string
    {
        $value = $this->get($key);
        if ($value === null) {
            throw new \RuntimeException("Missing required setting {$key}. Add it to .env.");
        }

        return $value;
    }

    public function bool(string $key, bool $default): bool
    {
        $value = $this->get($key);
        if ($value === null) {
            return $default;
        }

        return in_array(strtolower($value), ['1', 'true', 'yes', 'on'], true);
    }

    public function int(string $key, int $default): int
    {
        $value = $this->get($key);

        return $value === null || !is_numeric($value) ? $default : (int) $value;
    }
}
