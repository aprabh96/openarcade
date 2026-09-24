<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Support;

use OpenArcade\Support\HttpClient;

/** Scripted HTTP responses for tests; records every request it receives. */
final class FakeHttpClient implements HttpClient
{
    /** @var array<int, array{method:string,url:string,headers:array<string,string>,body:?string}> */
    public array $requests = [];

    /** @var array<int, array{status:int,body:string}|\RuntimeException> */
    private array $queue = [];

    public function queue(int $status, mixed $body): self
    {
        $this->queue[] = ['status' => $status, 'body' => is_string($body) ? $body : json_encode($body, JSON_THROW_ON_ERROR)];

        return $this;
    }

    public function queueFailure(string $message): self
    {
        $this->queue[] = new \RuntimeException($message);

        return $this;
    }

    public function request(string $method, string $url, array $headers, ?string $body, int $timeoutSeconds): array
    {
        $this->requests[] = ['method' => $method, 'url' => $url, 'headers' => $headers, 'body' => $body];
        $next = array_shift($this->queue);
        if ($next === null) {
            throw new \LogicException('FakeHttpClient: no response queued for ' . $method . ' ' . $url);
        }
        if ($next instanceof \RuntimeException) {
            throw $next;
        }

        return $next;
    }

    /** @return array<string,mixed> decoded JSON body of request $index */
    public function json(int $index): array
    {
        $decoded = json_decode((string) $this->requests[$index]['body'], true);

        return is_array($decoded) ? $decoded : [];
    }
}
