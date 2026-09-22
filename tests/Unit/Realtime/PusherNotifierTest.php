<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Realtime;

use ArcadeOS\Realtime\PusherNotifier;
use ArcadeOS\Support\FixedClock;
use ArcadeOS\Tests\Support\FakeHttpClient;
use PHPUnit\Framework\TestCase;

final class PusherNotifierTest extends TestCase
{
    public function testSendsTheLegacyContractWithAValidSignature(): void
    {
        $http = new FakeHttpClient();
        $http->queue(200, '{}');
        $clock = new FixedClock('2026-09-21 14:00:00');
        $notifier = new PusherNotifier($http, '123456', 'pusher-key', 'pusher-secret', 'us2', $clock);
        $notifier->stationCommand(3, 'START_SESSION', 60);

        $request = $http->requests[0];
        $parts = parse_url($request['url']);
        self::assertIsArray($parts);
        self::assertSame('api-us2.pusher.com', $parts['host']);
        self::assertSame('/apps/123456/events', $parts['path']);
        parse_str((string) ($parts['query'] ?? ''), $query);

        $body = (string) $request['body'];
        $event = json_decode($body, true);
        self::assertSame('station-3', $event['name']);
        self::assertSame(['vr-stations'], $event['channels']);
        self::assertSame(
            ['station' => 3, 'command' => 'START_SESSION', 'value' => 60, 'timestamp' => '2026-09-21T14:00:00+00:00'],
            json_decode((string) $event['data'], true)
        );

        $expectedParams = ['auth_key' => 'pusher-key', 'auth_timestamp' => (string) $clock->now()->getTimestamp(), 'auth_version' => '1.0', 'body_md5' => md5($body)];
        $expectedSignature = hash_hmac('sha256', "POST\n/apps/123456/events\n" . http_build_query($expectedParams), 'pusher-secret');
        self::assertSame($expectedSignature, $query['auth_signature']);
        self::assertSame(md5($body), $query['body_md5']);
    }

    public function testRejectsUnknownCommandsAndFailedDelivery(): void
    {
        $http = new FakeHttpClient();
        $notifier = new PusherNotifier($http, '1', 'k', 's', 'us2', new FixedClock('2026-09-21 14:00:00'));
        $this->expectException(\InvalidArgumentException::class);
        $notifier->stationCommand(1, 'REBOOT', 0);
    }

    public function testNon200IsADeliveryFailure(): void
    {
        $http = new FakeHttpClient();
        $http->queue(401, '{"error":"bad key"}');
        $notifier = new PusherNotifier($http, '1', 'k', 's', 'us2', new FixedClock('2026-09-21 14:00:00'));
        $this->expectException(\RuntimeException::class);
        $notifier->stationCommand(1, 'STOP_SESSION', 0);
    }
}
