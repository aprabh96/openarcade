<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\StationRepository;
use OpenArcade\Settings\SettingsRepository;
use PHPUnit\Framework\TestCase;

final class SettingsAndStationsTest extends TestCase
{
    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
    }

    public function testSettingsRoundTrip(): void
    {
        $repo = new SettingsRepository(TestDb::fresh());
        self::assertSame('My VR Arcade', $repo->load()->venueName);
        $repo->set('venue_name', 'Orbit VR');
        $repo->set('buffer_minutes', '15');
        $loaded = $repo->load();
        self::assertSame('Orbit VR', $loaded->venueName);
        self::assertSame(15, $loaded->bufferMinutes);
    }

    public function testSetRejectsUnknownKeysAndInvalidValues(): void
    {
        $repo = new SettingsRepository(TestDb::fresh());
        foreach ([['nonsense', '1'], ['timezone', 'Mars/Olympus']] as [$key, $value]) {
            try {
                $repo->set($key, $value);
                self::fail('expected InvalidArgumentException');
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }

    public function testSyncCountCreatesAndDeactivatesStations(): void
    {
        $stations = new StationRepository(TestDb::fresh());
        $stations->syncCount(4);
        self::assertSame([1, 2, 3, 4], array_values($stations->activeNumbersById()));
        $stations->syncCount(2);
        self::assertSame([1, 2], array_values($stations->activeNumbersById()));
        $stations->syncCount(3);
        self::assertSame([1, 2, 3], array_values($stations->activeNumbersById()));
    }
}
