<?php

declare(strict_types=1);

namespace ArcadeOS\Realtime;

use ArcadeOS\Support\Clock;
use ArcadeOS\Support\HttpClient;

/**
 * Pusher Channels over its signed REST API. Channel "vr-stations", event "station-{number}",
 * payload {station, command, value, timestamp}: the contract existing station clients already speak.
 */
final class PusherNotifier implements Notifier
{
    public const CHANNEL = 'vr-stations';

    public function __construct(
        private HttpClient $http,
        private string $appId,
        private string $key,
        private string $secret,
        private string $cluster,
        private Clock $clock,
    ) {
    }

    public function enabled(): bool
    {
        return true;
    }

    public function stationCommand(int $stationNumber, string $command, int|float $value): void
    {
        if (!in_array($command, self::COMMANDS, true)) {
            throw new \InvalidArgumentException('Unknown station command ' . $command);
        }
        $now = $this->clock->now();
        $data = ['station' => $stationNumber, 'command' => $command, 'value' => $value, 'timestamp' => $now->format(DATE_ATOM)];
        $body = json_encode([
            'name' => 'station-' . $stationNumber,
            'channels' => [self::CHANNEL],
            'data' => json_encode($data, JSON_THROW_ON_ERROR),
        ], JSON_THROW_ON_ERROR);

        $path = '/apps/' . $this->appId . '/events';
        $params = [
            'auth_key' => $this->key,
            'auth_timestamp' => (string) $now->getTimestamp(),
            'auth_version' => '1.0',
            'body_md5' => md5($body),
        ];
        ksort($params);
        $params['auth_signature'] = hash_hmac('sha256', "POST\n{$path}\n" . http_build_query($params), $this->secret);
        $url = 'https://api-' . $this->cluster . '.pusher.com' . $path . '?' . http_build_query($params);

        $response = $this->http->request('POST', $url, ['Content-Type' => 'application/json'], $body, 10);
        if ($response['status'] !== 200) {
            throw new \RuntimeException('Pusher responded with HTTP ' . $response['status']);
        }
    }
}
