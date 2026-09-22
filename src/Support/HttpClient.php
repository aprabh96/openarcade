<?php

declare(strict_types=1);

namespace ArcadeOS\Support;

/** The one seam between this application and outside HTTP services (Square, Pusher). */
interface HttpClient
{
    /**
     * @param array<string,string> $headers
     * @return array{status:int, body:string}
     * @throws \RuntimeException when the request could not be completed (DNS, connection, timeout)
     */
    public function request(string $method, string $url, array $headers, ?string $body, int $timeoutSeconds): array;
}
