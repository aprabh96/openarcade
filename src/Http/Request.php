<?php

declare(strict_types=1);

namespace OpenArcade\Http;

final class Request
{
    /**
     * @param array<string,string> $query
     * @param array<string,mixed> $body decoded JSON object (or form fields)
     * @param array<string,string> $headers lower-case names
     */
    public function __construct(
        public readonly string $method,
        public readonly string $path,
        public readonly array $query,
        public readonly array $body,
        public readonly array $headers,
        public readonly string $ip,
        public readonly Session $session,
        public readonly bool $secure = false,
        public readonly bool $malformedBody = false,
    ) {
    }

    public static function fromGlobals(Session $session, bool $trustProxy = false): self
    {
        $server = $_SERVER;
        $method = strtoupper((string) ($server['REQUEST_METHOD'] ?? 'GET'));
        $uri = (string) ($server['REQUEST_URI'] ?? '/');
        $path = (string) (parse_url($uri, PHP_URL_PATH) ?: '/');
        $path = rawurldecode($path);
        if ($path !== '/' && str_ends_with($path, '/')) {
            $path = rtrim($path, '/');
        }

        $headers = [];
        foreach ($server as $name => $value) {
            if (str_starts_with((string) $name, 'HTTP_')) {
                $headers[strtolower(str_replace('_', '-', substr((string) $name, 5)))] = (string) $value;
            }
        }
        if (isset($server['CONTENT_TYPE'])) {
            $headers['content-type'] = (string) $server['CONTENT_TYPE'];
        }

        $ip = (string) ($server['REMOTE_ADDR'] ?? '0.0.0.0');
        $secure = isset($server['HTTPS']) && $server['HTTPS'] !== '' && $server['HTTPS'] !== 'off';
        if ($trustProxy) {
            if (isset($headers['x-forwarded-for'])) {
                // The proxy appends the address it saw; everything to its left came from the client.
                $parts = explode(',', $headers['x-forwarded-for']);
                $last = trim((string) end($parts));
                if (filter_var($last, FILTER_VALIDATE_IP) !== false) {
                    $ip = $last;
                }
            }
            if (isset($headers['x-forwarded-proto'])) {
                $secure = strtolower(trim($headers['x-forwarded-proto'])) === 'https';
            }
        }

        $query = [];
        foreach ($_GET as $key => $value) {
            if (is_string($value)) {
                $query[(string) $key] = $value;
            }
        }

        $body = [];
        $malformed = false;
        $contentType = strtolower($headers['content-type'] ?? '');
        if (str_contains($contentType, 'application/json')) {
            $raw = (string) file_get_contents('php://input');
            if (trim($raw) !== '') {
                $decoded = json_decode($raw, true);
                if (is_array($decoded)) {
                    $body = $decoded;
                } else {
                    $malformed = true;
                }
            }
        } elseif ($method !== 'GET') {
            $body = $_POST;
        }

        return new self($method, $path, $query, $body, $headers, $ip, $session, $secure, $malformed);
    }

    public function header(string $name): ?string
    {
        return $this->headers[strtolower($name)] ?? null;
    }

    public function query(string $key): ?string
    {
        return $this->query[$key] ?? null;
    }

    /** A body field as a trimmed string, or null when absent or not scalar. */
    public function string(string $key): ?string
    {
        $value = $this->body[$key] ?? null;
        if ($value === null || is_array($value) || is_object($value)) {
            return null;
        }
        if (is_bool($value)) {
            return $value ? '1' : '0';
        }

        return trim((string) $value);
    }

    /** A body field as an int, or null when absent or not an integer. */
    public function int(string $key): ?int
    {
        $value = $this->body[$key] ?? null;
        if (is_int($value)) {
            return $value;
        }
        if (is_string($value) && preg_match('/^-?\d{1,9}$/', trim($value)) === 1) {
            return (int) trim($value);
        }

        return null;
    }

    public function bool(string $key): ?bool
    {
        $value = $this->body[$key] ?? null;
        if (is_bool($value)) {
            return $value;
        }
        if (is_string($value) || is_int($value)) {
            $normalized = strtolower((string) $value);
            if (in_array($normalized, ['1', 'true', 'yes', 'on'], true)) {
                return true;
            }
            if (in_array($normalized, ['0', 'false', 'no', 'off'], true)) {
                return false;
            }
        }

        return null;
    }
}
