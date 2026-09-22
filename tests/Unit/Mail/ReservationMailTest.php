<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Mail;

use ArcadeOS\Mail\ReservationMail;
use ArcadeOS\Settings\VenueSettings;
use PHPUnit\Framework\TestCase;

final class ReservationMailTest extends TestCase
{
    /** @return array<string,mixed> */
    private function row(): array
    {
        return [
            'id' => 7, 'confirmation_code' => 'ABCD2345', 'status' => 'confirmed', 'first_name' => 'Alex', 'last_name' => 'Rivera',
            'email' => 'alex.rivera@example.com', 'phone' => '785-555-0142', 'comments' => 'Birthday', 'date' => '2026-09-22',
            'start_minute' => 600, 'duration_minutes' => 90, 'stations' => [1, 2], 'total_cents' => 7761, 'currency' => 'USD',
            'payment_id' => null, 'payment_provider' => 'none',
        ];
    }

    public function testCustomerEmailHasTheEssentials(): void
    {
        $venue = VenueSettings::fromArray(['venue_name' => 'Orbit VR', 'venue_address' => '1 Main St', 'venue_phone' => '785-555-0100']);
        $mail = ReservationMail::customer($this->row(), $venue);
        self::assertSame('Booking confirmed: Orbit VR, Tue Sep 22', $mail['subject']);
        self::assertStringContainsString('Confirmation code: ABCD2345', $mail['text']);
        self::assertStringContainsString('Tuesday, September 22, 2026 at 10:00 AM to 11:30 AM', $mail['text']);
        self::assertStringContainsString('Stations: 2 (#1, #2)', $mail['text']);
        self::assertStringContainsString('USD 77.61 (payable at the venue)', $mail['text']);
        self::assertStringContainsString('1 Main St', $mail['text']);
        self::assertStringContainsString('785-555-0100', $mail['text']);
    }

    public function testVenueEmailIncludesContactDetailsAndPaymentState(): void
    {
        $row = $this->row() + [];
        $row['payment_id'] = 'PAY1';
        $row['payment_provider'] = 'square';
        $mail = ReservationMail::venue($row, VenueSettings::fromArray([]));
        self::assertStringStartsWith('New booking ABCD2345: Tuesday', $mail['subject']);
        self::assertStringContainsString('alex.rivera@example.com', $mail['text']);
        self::assertStringContainsString('(paid, square)', $mail['text']);
        self::assertStringContainsString('Comments: Birthday', $mail['text']);
    }
}
