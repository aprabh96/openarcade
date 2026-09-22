<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Api;

use ArcadeOS\Domain\BookingRules;
use ArcadeOS\Payments\PaymentResult;
use ArcadeOS\Tests\Integration\VenueFixture;
use ArcadeOS\Tests\Support\FakeGateway;

final class PaymentFlowTest extends ApiTestCase
{
    private FakeGateway $gateway;

    protected function setUp(): void
    {
        parent::setUp();
        $this->gateway = new FakeGateway();
        $this->boot($this->gateway);
    }

    /** @return array<string,mixed> */
    private function paidBooking(): array
    {
        return $this->customerBooking() + ['payment_token' => 'cnon:card-nonce'];
    }

    public function testVenueAdvertisesSquareAndMissingTokenIsAFieldError(): void
    {
        $venue = $this->json($this->request('GET', '/api/venue'));
        self::assertSame(['mode' => 'square', 'square' => ['application_id' => 'sandbox-app-id', 'location_id' => 'LOC123', 'environment' => 'sandbox']], $venue['payment']);

        $response = $this->request('POST', '/api/reservations', $this->customerBooking());
        self::assertSame(422, $response->status);
        self::assertArrayHasKey('payment_token', $this->json($response)['error']['details']['fields']);
        self::assertSame([], $this->gateway->chargeCalls);
    }

    public function testSuccessfulPaymentConfirmsTheBookingWithTheServerAmount(): void
    {
        $this->gateway->willReturn(PaymentResult::paid('PAY-1'));
        $response = $this->request('POST', '/api/reservations', $this->paidBooking());
        self::assertSame(201, $response->status, $response->body);
        $reservation = $this->json($response)['reservation'];
        self::assertSame('confirmed', $reservation['status']);
        self::assertSame(2734, $this->gateway->chargeCalls[0]['amountCents']);
        self::assertSame($reservation['confirmation_code'], $this->gateway->chargeCalls[0]['referenceCode']);
        self::assertSame('cnon:card-nonce', $this->gateway->chargeCalls[0]['sourceToken']);

        $this->signIn();
        $row = $this->json($this->request('GET', '/api/admin/reservations?date=2026-09-22'))['reservations'][0];
        self::assertSame('PAY-1', $row['payment_id']);
        self::assertSame('square', $row['payment_provider']);
        self::assertCount(1, $this->mailer->sent);
    }

    public function testDeclinedPaymentReleasesTheStation(): void
    {
        $this->gateway->willReturn(PaymentResult::declined('The card was declined.'));
        $response = $this->request('POST', '/api/reservations', $this->paidBooking());
        self::assertSame(402, $response->status);
        self::assertSame('payment_declined', $this->json($response)['error']['code']);
        self::assertSame([], $this->mailer->sent);
        $free = $this->json($this->request('GET', '/api/availability?date=2026-09-22&duration=60'));
        self::assertSame(2, array_column($free['slots'], 'free', 'start_minute')[600]);
    }

    public function testUnknownOutcomeKeepsTheHoldUntilItIsReconciled(): void
    {
        $this->gateway->willReturn(PaymentResult::unknown('no answer'));
        $response = $this->request('POST', '/api/reservations', $this->paidBooking());
        self::assertSame(503, $response->status);
        self::assertSame('payment_unknown', $this->json($response)['error']['code']);
        $taken = $this->json($this->request('GET', '/api/availability?date=2026-09-22&duration=60'));
        self::assertSame(1, array_column($taken['slots'], 'free', 'start_minute')[600], 'the hold still blocks the station');

        // The hold passes. Reconciliation finds the completed payment and confirms the booking.
        $this->clock->advanceMinutes(11);
        $this->gateway->lookup = PaymentResult::paid('PAY-LATE');
        $report = $this->services->reservations->reconcileHolds($this->gateway, $this->logger);
        self::assertSame(['confirmed' => 1, 'expired' => 0, 'refunded' => 0], $report);
        $this->signIn();
        $row = $this->json($this->request('GET', '/api/admin/reservations?date=2026-09-22'))['reservations'][0];
        self::assertSame('confirmed', $row['status']);
        self::assertSame('PAY-LATE', $row['payment_id']);
    }

    public function testUnknownOutcomeWithNoPaymentFoundExpiresTheHold(): void
    {
        $this->gateway->willReturn(PaymentResult::unknown('no answer'));
        $this->request('POST', '/api/reservations', $this->paidBooking());
        $this->clock->advanceMinutes(11);
        $this->gateway->lookup = null;
        self::assertSame(['confirmed' => 0, 'expired' => 1, 'refunded' => 0], $this->services->reservations->reconcileHolds($this->gateway, $this->logger));
        $free = $this->json($this->request('GET', '/api/availability?date=2026-09-22&duration=60'));
        self::assertSame(2, array_column($free['slots'], 'free', 'start_minute')[600]);
    }

    public function testLatePaymentForALostSlotIsRefunded(): void
    {
        // One station is already taken. The customer's charge gets no answer, the hold lapses, a
        // walk-in takes the last station, and then the payment turns out to have gone through.
        $this->services->reservations->create(VenueFixture::request('2026-09-22', 600, 60, 1), BookingRules::customer(false));
        $this->gateway->willReturn(PaymentResult::unknown('no answer'));
        self::assertSame(503, $this->request('POST', '/api/reservations', $this->paidBooking())->status);
        $this->clock->advanceMinutes(11);
        $this->services->reservations->create(VenueFixture::request('2026-09-22', 600, 60, 1, 'sam.okafor@example.com'), BookingRules::customer(false));
        $this->gateway->lookup = PaymentResult::paid('PAY-3');
        $report = $this->services->reservations->reconcileHolds($this->gateway, $this->logger);
        self::assertSame(['confirmed' => 0, 'expired' => 0, 'refunded' => 1], $report);
        self::assertSame('PAY-3', $this->gateway->refunds[0]['paymentId']);
        self::assertSame(2734, $this->gateway->refunds[0]['amountCents']);
    }
}
