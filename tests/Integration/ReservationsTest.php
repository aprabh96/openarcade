<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Integration;

use OpenArcade\Domain\BookingRejected;
use OpenArcade\Domain\BookingRequest;
use OpenArcade\Domain\BookingRules;
use OpenArcade\Domain\HoursRepository;
use OpenArcade\Domain\Reservations;
use OpenArcade\Support\FixedClock;
use PDO;
use PHPUnit\Framework\TestCase;

final class ReservationsTest extends TestCase
{
    private PDO $pdo;
    private FixedClock $clock;
    private Reservations $service;

    protected function setUp(): void
    {
        if (!TestDb::available()) {
            self::markTestSkipped('Test database not configured.');
        }
        $this->pdo = TestDb::fresh();
        VenueFixture::seed($this->pdo, 2);
        $this->clock = new FixedClock('2026-09-21 14:00:00');
        $this->service = Reservations::build($this->pdo, $this->clock);
    }

    private function rejects(string $reason, BookingRequest $request, ?BookingRules $rules = null): BookingRejected
    {
        try {
            $this->service->create($request, $rules ?? BookingRules::customer(false));
        } catch (BookingRejected $rejected) {
            self::assertSame($reason, $rejected->reason);

            return $rejected;
        }
        self::fail("expected rejection {$reason}");
    }

    public function testCustomerBookingIsConfirmedPricedAndStoredInUtc(): void
    {
        $reservation = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(false));
        self::assertSame('confirmed', $reservation->status);
        self::assertSame(2500, $reservation->subtotalCents);
        self::assertSame(234, $reservation->taxCents);
        self::assertSame(2734, $reservation->totalCents);
        self::assertSame('2026-09-22 15:00:00', $reservation->startUtc);
        self::assertSame('2026-09-22 16:00:00', $reservation->endUtc);
        self::assertSame(600, $reservation->startMinute);
        self::assertCount(1, $reservation->stationIds);
        self::assertMatchesRegularExpression('/^[A-HJ-NP-Z2-9]{8}$/', $reservation->confirmationCode);
        self::assertNull($reservation->holdExpiresAtUtc);
    }

    public function testPaymentRequiredCreatesAHoldThatExpires(): void
    {
        $reservation = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        self::assertSame('held', $reservation->status);
        self::assertSame('2026-09-21 14:10:00', $reservation->holdExpiresAtUtc);
    }

    public function testSaturdayUsesTheWeekdayOverride(): void
    {
        $reservation = $this->service->create(VenueFixture::request('2026-09-26', 600), BookingRules::customer(false));
        self::assertSame(3000, $reservation->subtotalCents);
    }

    public function testSecondBookingGetsTheOtherStationAndThirdIsRefused(): void
    {
        $first = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(false));
        $second = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(false));
        self::assertNotSame($first->stationIds, $second->stationIds);
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 600));
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 630));
    }

    public function testBufferBlocksBackToBackButAllowsTheNextGridSlot(): void
    {
        $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 660));
        $later = $this->service->create(VenueFixture::request('2026-09-22', 690), BookingRules::customer(false));
        self::assertSame('confirmed', $later->status);
    }

    public function testCustomerRuleRejections(): void
    {
        $this->rejects('date_in_past', VenueFixture::request('2026-09-20', 600));
        $this->rejects('too_far_ahead', VenueFixture::request('2027-01-15', 600));
        $this->rejects('outside_hours', VenueFixture::request('2026-09-22', 540));
        $this->rejects('outside_hours', VenueFixture::request('2026-09-22', 1290, 60));
        $this->rejects('off_grid', VenueFixture::request('2026-09-22', 615));
        $this->rejects('duration_not_offered', VenueFixture::request('2026-09-22', 600, 45));
        $this->rejects('invalid_station_count', VenueFixture::request('2026-09-22', 600, 60, 3));
        $rejected = $this->rejects('validation_failed', VenueFixture::request('2026-09-22', 600, 60, 1, 'not-an-email'));
        self::assertArrayHasKey('email', $rejected->fieldErrors);
    }

    public function testTooSoonUsesLeadTimeOnTheSameDay(): void
    {
        // Now is 09:00 local, opening is 10:00, lead time 30 minutes: 10:00 is fine.
        $ok = $this->service->create(VenueFixture::request('2026-09-21', 600), BookingRules::customer(false));
        self::assertSame('confirmed', $ok->status);
        $this->clock->advanceMinutes(45); // 09:45 local; 10:00 is now inside the 30 minute lead time
        $this->rejects('too_soon', VenueFixture::request('2026-09-21', 600));
    }

    public function testClosedDayIsRefused(): void
    {
        (new HoursRepository($this->pdo))->addClosedDate('2026-09-23', 'Holiday');
        $this->rejects('closed', VenueFixture::request('2026-09-23', 600));
    }

    public function testAdminMayBookOffGridInsideLeadTimeAndComplimentaryButNeverOverlap(): void
    {
        $this->clock->advanceMinutes(75); // 10:15 local
        $walkIn = $this->service->create(
            new BookingRequest('2026-09-21', 617, 45, 2, 'Walk', 'In', '', '', 'paid at counter'),
            BookingRules::admin(true)
        );
        self::assertSame('confirmed', $walkIn->status);
        self::assertSame(0, $walkIn->totalCents);
        self::assertCount(2, $walkIn->stationIds);
        $this->rejects('slot_unavailable', new BookingRequest('2026-09-21', 640, 45, 1, 'Second', 'Group', '', ''), BookingRules::admin(true));
        $this->rejects('duration_not_offered', new BookingRequest('2026-09-21', 900, 45, 1, 'No', 'Price', '', ''), BookingRules::admin(false));
    }

    public function testExpiredHoldFreesTheStation(): void
    {
        $pdo = TestDb::fresh();
        VenueFixture::seed($pdo, 1);
        $clock = new FixedClock('2026-09-21 14:00:00');
        $service = Reservations::build($pdo, $clock);
        $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        try {
            $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
            self::fail('expected slot_unavailable while the hold is live');
        } catch (BookingRejected $rejected) {
            self::assertSame('slot_unavailable', $rejected->reason);
        }
        $clock->advanceMinutes(11);
        $second = $service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        self::assertSame('held', $second->status);
        self::assertSame(1, $service->expireHolds());
    }

    public function testConfirmPaymentIsIdempotentAndFailAndCancelFreeTheStation(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $confirmed = $this->service->confirmPayment($held->id, 'square', 'pay_123');
        self::assertSame('confirmed', $confirmed->status);
        self::assertSame('pay_123', $confirmed->paymentId);
        self::assertSame('confirmed', $this->service->confirmPayment($held->id, 'square', 'pay_123')->status);

        $this->service->cancel($held->id);
        $again = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $this->service->failPayment($again->id);
        $third = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        self::assertSame('confirmed', $third->status);

        try {
            $this->service->cancel($again->id);
            self::fail('expected wrong_status');
        } catch (BookingRejected $rejected) {
            self::assertSame('wrong_status', $rejected->reason);
        }
    }

    public function testConfirmAfterExpiryWithStationsTakenExpiresTheHoldAndTellsCallerToRefund(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        try {
            $this->service->confirmPayment($held->id, 'square', 'pay_late');
            self::fail('expected hold_expired');
        } catch (BookingRejected $rejected) {
            self::assertSame('hold_expired', $rejected->reason);
        }
        $status = $this->pdo->query('SELECT status FROM reservations WHERE id = ' . $held->id);
        self::assertSame('expired', $status === false ? null : $status->fetchColumn());
    }

    public function testConfirmAfterExpiryStillWorksWhenStationsAreStillFree(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        self::assertSame('confirmed', $this->service->confirmPayment($held->id, 'square', 'pay_slow')->status);
    }

    public function testLatePaymentAfterTheSweepIsHonouredWhenStationsAreStillFree(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        self::assertSame(1, $this->service->expireHolds());
        $confirmed = $this->service->confirmPayment($held->id, 'square', 'pay_after_sweep');
        self::assertSame('confirmed', $confirmed->status);
        self::assertSame('pay_after_sweep', $confirmed->paymentId);
    }

    public function testLatePaymentAfterTheSweepIsRefusedWhenStationsWereTaken(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        self::assertSame(1, $this->service->expireHolds());
        $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        try {
            $this->service->confirmPayment($held->id, 'square', 'pay_too_late');
            self::fail('expected hold_expired');
        } catch (BookingRejected $rejected) {
            self::assertSame('hold_expired', $rejected->reason);
        }
    }

    public function testCancelledReservationCannotBeConfirmed(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        $this->service->cancel($held->id);
        try {
            $this->service->confirmPayment($held->id, 'square', 'pay_x');
            self::fail('expected wrong_status');
        } catch (BookingRejected $rejected) {
            self::assertSame('wrong_status', $rejected->reason);
        }
    }

    public function testSessionEndingAtLocalMidnight(): void
    {
        (new HoursRepository($this->pdo))->setWeekday(2, 600, 1440, false);
        $first = $this->service->create(VenueFixture::request('2026-09-22', 1380, 60, 2), BookingRules::customer(false));
        self::assertSame('confirmed', $first->status);
        self::assertSame('2026-09-23 05:00:00', $first->endUtc);

        // Same slot again: both stations are taken 23:00-00:00.
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 1380));

        // 21:30-22:30 leaves a 30 minute gap before the 23:00 session, which clears the 10 minute buffer.
        $buffered = $this->service->create(VenueFixture::request('2026-09-22', 1290), BookingRules::customer(false));
        self::assertSame('confirmed', $buffered->status);

        // 22:30-23:30 runs straight into the 23:00 session.
        $this->rejects('slot_unavailable', VenueFixture::request('2026-09-22', 1350));
    }

    public function testDaylightSavingChangeKeepsWallClockTime(): void
    {
        $clock = new FixedClock('2026-03-01 14:00:00');
        $service = Reservations::build($this->pdo, $clock);
        $before = $service->create(VenueFixture::request('2026-03-07', 600), BookingRules::customer(false));
        $after = $service->create(VenueFixture::request('2026-03-08', 600), BookingRules::customer(false));
        self::assertSame('2026-03-07 16:00:00', $before->startUtc, '10:00 CST');
        self::assertSame('2026-03-08 15:00:00', $after->startUtc, '10:00 CDT');
        self::assertSame(600, $after->startMinute);
    }
}
