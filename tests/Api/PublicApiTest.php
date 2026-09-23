<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Api;

use ArcadeOS\Domain\BookingRules;
use ArcadeOS\Settings\SettingsRepository;
use ArcadeOS\Tests\Integration\VenueFixture;

final class PublicApiTest extends ApiTestCase
{
    public function testVenueDescribesTheVenueWithoutPersonalData(): void
    {
        (new SettingsRepository($this->pdo))->set('notification_email', 'owner@example.com');
        $this->services->reservations->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(false));

        $response = $this->request('GET', '/api/venue');
        self::assertSame(200, $response->status);
        $data = $this->json($response);
        self::assertSame('My VR Arcade', $data['venue']['name']);
        self::assertSame('America/Chicago', $data['venue']['timezone']);
        self::assertSame(935, $data['venue']['tax_rate_bp']);
        self::assertSame(2, $data['venue']['station_count']);
        self::assertSame([60, 90], $data['durations']);
        self::assertSame('none', $data['payment']['mode']);
        self::assertSame('2026-09-21', $data['today']);
        self::assertSame(540, $data['now_minute']);
        self::assertCount(7, $data['hours']);
        self::assertStringNotContainsString('example.com', $response->body);
        self::assertStringNotContainsString('Alex', $response->body);
        self::assertStringNotContainsString('owner', $response->body);
    }

    public function testAvailabilityReflectsExistingBookingsAndTheBuffer(): void
    {
        $this->services->reservations->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));

        $response = $this->request('GET', '/api/availability?date=2026-09-22&duration=60&stations=1');
        self::assertSame(200, $response->status, $response->body);
        $data = $this->json($response);
        self::assertFalse($data['closed']);
        self::assertSame(600, $data['open_minute']);
        $byStart = array_column($data['slots'], 'free', 'start_minute');
        self::assertSame(0, $byStart[600]);
        self::assertSame(0, $byStart[630]);
        self::assertSame(0, $byStart[660], 'inside the 10 minute buffer');
        self::assertSame(2, $byStart[690]);
        self::assertSame(1260, max(array_keys($byStart)), 'last 60 minute slot ends at close (22:00)');
        self::assertStringNotContainsString('Alex', $response->body);
    }

    public function testAvailabilityTodayStartsAfterTheLeadTime(): void
    {
        $this->clock->advanceMinutes(90); // 10:30 local, lead 30 -> earliest 11:00
        $data = $this->json($this->request('GET', '/api/availability?date=2026-09-21&duration=60'));
        self::assertSame(660, $data['slots'][0]['start_minute']);
    }

    public function testAvailabilityRejectsBadInput(): void
    {
        self::assertSame(422, $this->request('GET', '/api/availability?duration=60')->status);
        self::assertSame(422, $this->request('GET', '/api/availability?date=22-09-2026&duration=60')->status);
        self::assertSame('date_in_past', $this->json($this->request('GET', '/api/availability?date=2026-09-20&duration=60'))['error']['code']);
        self::assertSame('too_far_ahead', $this->json($this->request('GET', '/api/availability?date=2027-06-01&duration=60'))['error']['code']);
        self::assertSame('duration_not_offered', $this->json($this->request('GET', '/api/availability?date=2026-09-22&duration=45'))['error']['code']);
    }

    public function testClosedDateReportsClosedWithNoSlots(): void
    {
        $this->services->hours->addClosedDate('2026-09-23', 'Holiday');
        $data = $this->json($this->request('GET', '/api/availability?date=2026-09-23&duration=60'));
        self::assertTrue($data['closed']);
        self::assertSame([], $data['slots']);
    }

    public function testBookingCreatesAConfirmedReservationAndSendsEmails(): void
    {
        (new SettingsRepository($this->pdo))->set('notification_email', 'owner@example.com');
        $response = $this->request('POST', '/api/reservations', $this->customerBooking());
        self::assertSame(201, $response->status, $response->body);
        $reservation = $this->json($response)['reservation'];
        self::assertSame('confirmed', $reservation['status']);
        self::assertMatchesRegularExpression('/^[A-HJ-NP-Z2-9]{8}$/', $reservation['confirmation_code']);
        self::assertSame(2734, $reservation['total_cents']);
        self::assertSame([1], $reservation['stations']);
        self::assertArrayNotHasKey('email', $reservation);
        self::assertArrayNotHasKey('id', $reservation);

        self::assertCount(2, $this->mailer->sent);
        self::assertSame('alex.rivera@example.com', $this->mailer->sent[0]['to']);
        self::assertStringContainsString($reservation['confirmation_code'], $this->mailer->sent[0]['text']);
        self::assertStringContainsString('Tuesday, September 22, 2026 at 10:00 AM to 11:00 AM', $this->mailer->sent[0]['text']);
        self::assertSame('owner@example.com', $this->mailer->sent[1]['to']);
        self::assertStringContainsString('785-555-0142', $this->mailer->sent[1]['text']);
    }

    public function testBookingTokenIsRequiredSignedAndExpires(): void
    {
        $body = $this->customerBooking();
        $token = $body['booking_token'];

        $missing = $body;
        unset($missing['booking_token']);
        self::assertSame('booking_expired', $this->json($this->request('POST', '/api/reservations', $missing))['error']['code']);

        $forged = $body;
        $forged['booking_token'] = substr($token, 0, -64) . str_repeat('0', 64);
        self::assertSame(403, $this->request('POST', '/api/reservations', $forged)->status);

        $this->clock->advanceMinutes(5 * 60);
        self::assertSame(403, $this->request('POST', '/api/reservations', $body)->status, 'stale token');
    }

    public function testARetriedRequestReturnsTheFirstBooking(): void
    {
        $body = $this->customerBooking() + ['request_id' => 'attempt-0123456789abcdef'];
        $first = $this->request('POST', '/api/reservations', $body);
        self::assertSame(201, $first->status, $first->body);
        $again = $this->request('POST', '/api/reservations', $body);
        self::assertSame(201, $again->status, $again->body);
        self::assertSame($this->json($first)['reservation']['confirmation_code'], $this->json($again)['reservation']['confirmation_code']);
        self::assertSame(1, (int) $this->pdo->query('SELECT COUNT(*) FROM reservations')->fetchColumn());
        self::assertCount(1, $this->mailer->sent, 'the confirmation email is sent once');

        $bad = $this->customerBooking() + ['request_id' => 'short'];
        self::assertSame(422, $this->request('POST', '/api/reservations', $bad)->status);
    }

    public function testBookingRefusesCrossSiteOrigins(): void
    {
        $body = $this->customerBooking();
        self::assertSame(403, $this->request('POST', '/api/reservations', $body, ['Origin' => 'https://evil.example.net'])->status);
        self::assertSame('cross_site', $this->json($this->request('POST', '/api/reservations', $body, ['Origin' => 'https://evil.example.net', 'Host' => 'evil.example.net']))['error']['code'], 'the Host header is not trusted');
        self::assertSame(403, $this->request('POST', '/api/reservations', $body, ['no-origin' => '1'])->status);
        self::assertSame(201, $this->request('POST', '/api/reservations', $body, ['no-origin' => '1', 'Referer' => 'http://localhost/book/?x=1'])->status);
    }

    public function testBookingValidationListsFields(): void
    {
        $body = $this->customerBooking(email: 'not-an-email');
        $body['phone'] = '';
        $response = $this->request('POST', '/api/reservations', $body);
        self::assertSame(422, $response->status);
        $error = $this->json($response)['error'];
        self::assertSame('validation_failed', $error['code']);
        self::assertArrayHasKey('email', $error['details']['fields']);
        self::assertArrayHasKey('phone', $error['details']['fields']);
    }

    public function testBookingConflictIs409AndRuleBreaksAre422(): void
    {
        $this->services->reservations->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        $conflict = $this->request('POST', '/api/reservations', $this->customerBooking());
        self::assertSame(409, $conflict->status);
        self::assertSame('slot_unavailable', $this->json($conflict)['error']['code']);

        $offGrid = $this->request('POST', '/api/reservations', $this->customerBooking(start: 615));
        self::assertSame(422, $offGrid->status);
        self::assertSame('off_grid', $this->json($offGrid)['error']['code']);
    }

    public function testMalformedJsonIs400(): void
    {
        $response = $this->request('POST', '/api/reservations', [], [], '203.0.113.5', true);
        self::assertSame(400, $response->status);
    }

    public function testBookingsAreRateLimitedPerClient(): void
    {
        $body = $this->customerBooking(email: 'bad');
        for ($i = 1; $i <= 10; $i++) {
            self::assertSame(422, $this->request('POST', '/api/reservations', $body)->status, "attempt {$i}");
        }
        self::assertSame(429, $this->request('POST', '/api/reservations', $body)->status);
        self::assertSame(422, $this->request('POST', '/api/reservations', $body, [], '198.51.100.9')->status, 'another client is unaffected');
    }

    public function testUnknownEndpointsAndMethods(): void
    {
        self::assertSame(404, $this->request('GET', '/api/nothing')->status);
        self::assertSame(405, $this->request('DELETE', '/api/venue')->status);
    }

    public function testSecurityHeadersAreOnEveryResponse(): void
    {
        $response = $this->request('GET', '/api/venue');
        self::assertSame('nosniff', $response->headers['X-Content-Type-Options']);
        self::assertStringContainsString("frame-ancestors 'self' https://venue.example.com", $response->headers['Content-Security-Policy']);
        $missing = $this->request('GET', '/api/nothing');
        self::assertArrayHasKey('Content-Security-Policy', $missing->headers);
    }
}
