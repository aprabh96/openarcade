<?php

declare(strict_types=1);

namespace ArcadeOS\Realtime;

interface Notifier
{
    public const COMMANDS = ['START_SESSION', 'STOP_SESSION', 'ADD_TIME'];

    /** Whether station commands reach the stations at all (false for the "none" driver). */
    public function enabled(): bool;

    /**
     * Sends one command to one station. Contract: channel "vr-stations", event "station-{number}",
     * payload {station, command, value, timestamp}. See docs/realtime-contract.md.
     *
     * @throws \RuntimeException when the message could not be delivered
     */
    public function stationCommand(int $stationNumber, string $command, int|float $value): void;
}
