<?php

declare(strict_types=1);

namespace ArcadeOS\Realtime;

final class NullNotifier implements Notifier
{
    /** @var array<int, array{station:int,command:string,value:int|float}> */
    public array $sent = [];

    public function enabled(): bool
    {
        return false;
    }

    public function stationCommand(int $stationNumber, string $command, int|float $value): void
    {
        $this->sent[] = ['station' => $stationNumber, 'command' => $command, 'value' => $value];
    }
}
