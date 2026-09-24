<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Settings;

use OpenArcade\Settings\VenueSettings;
use PHPUnit\Framework\TestCase;

final class VenueSettingsTest extends TestCase
{
    public function testDefaultsFillMissingValues(): void
    {
        $settings = VenueSettings::fromArray(['venue_name' => 'Orbit VR', 'tax_rate_bp' => '935']);
        self::assertSame('Orbit VR', $settings->venueName);
        self::assertSame(935, $settings->taxRateBp);
        self::assertSame(30, $settings->slotStepMinutes);
        self::assertSame(10, $settings->bufferMinutes);
        self::assertSame('America/Chicago', $settings->tz()->getName());
    }

    public function testRejectsInvalidTimezoneAndOutOfRangeNumbers(): void
    {
        foreach ([['timezone' => 'Mars/Olympus'], ['slot_step_minutes' => '0'], ['buffer_minutes' => '-5'], ['currency' => 'usd'], ['tax_rate_bp' => '10001']] as $bad) {
            try {
                VenueSettings::fromArray($bad);
                self::fail('expected InvalidArgumentException for ' . json_encode($bad));
            } catch (\InvalidArgumentException) {
                self::assertTrue(true);
            }
        }
    }
}
