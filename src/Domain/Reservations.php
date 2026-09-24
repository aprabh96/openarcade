<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

use OpenArcade\Db\Transaction;
use OpenArcade\Payments\PaymentGateway;
use OpenArcade\Payments\PaymentLookupFailed;
use OpenArcade\Payments\PaymentResult;
use OpenArcade\Settings\SettingsRepository;
use OpenArcade\Settings\VenueSettings;
use OpenArcade\Support\Clock;
use OpenArcade\Support\Logger;
use PDO;

final class Reservations
{
    public function __construct(
        private PDO $pdo,
        private ReservationRepository $reservations,
        private HoursRepository $hours,
        private PriceRepository $prices,
        private StationRepository $stations,
        private SettingsRepository $settings,
        private Clock $clock,
    ) {
    }

    public static function build(PDO $pdo, Clock $clock): self
    {
        return new self(
            $pdo,
            new ReservationRepository($pdo),
            new HoursRepository($pdo),
            new PriceRepository($pdo),
            new StationRepository($pdo),
            new SettingsRepository($pdo),
            $clock,
        );
    }

    /** @throws BookingRejected */
    public function create(BookingRequest $request, BookingRules $rules, ?string $requestId = null): Reservation
    {
        $errors = $request->validate($rules->contactRequired);
        if ($errors !== []) {
            throw new BookingRejected('validation_failed', $errors);
        }

        $settings = $this->settings->load();
        $tz = $settings->tz();
        $now = $this->clock->now();
        $hours = $this->checkRules($request, $rules, $settings, $now->setTimezone($tz));

        $numbersById = $this->stations->activeNumbersById();
        if ($request->stationCount < 1 || $request->stationCount > count($numbersById)) {
            throw new BookingRejected('invalid_station_count');
        }

        $quote = $rules->complimentary
            ? new Quote(0, 0, 0, $settings->currency)
            : $this->quote($request, $settings);

        [$startUtc, $endUtc] = self::utcRange($request, $tz);
        $endMinute = $request->startMinute + $request->durationMinutes;
        $held = $rules->requirePayment && $quote->totalCents > 0;
        $openMinute = $hours === null ? 0 : $hours->openMinute;

        $this->reservations->ensureDayRow($request->localDate);

        return Transaction::run($this->pdo, function () use ($request, $rules, $settings, $tz, $now, $numbersById, $quote, $startUtc, $endUtc, $endMinute, $held, $openMinute, $requestId): Reservation {
            $this->reservations->lockDay($request->localDate);
            $blocks = $this->reservations->blocksForDate($request->localDate, $now, $tz);
            $free = Availability::freeStations(array_keys($numbersById), $blocks, $request->startMinute, $endMinute, $settings->bufferMinutes);
            $chosen = StationAllocator::choose($free, $blocks, $numbersById, $request->startMinute, $request->stationCount, $openMinute);
            if ($chosen === null) {
                throw new BookingRejected('slot_unavailable');
            }
            $timestamp = $now->format('Y-m-d H:i:s');
            $id = $this->reservations->insert([
                'status' => $held ? 'held' : 'confirmed',
                'first_name' => trim($request->firstName),
                'last_name' => trim($request->lastName),
                'email' => trim($request->email),
                'phone' => trim($request->phone),
                'comments' => $request->comments === null ? null : trim($request->comments),
                'local_date' => $request->localDate,
                'start_utc' => $startUtc,
                'end_utc' => $endUtc,
                'duration_minutes' => $request->durationMinutes,
                'station_count' => $request->stationCount,
                'subtotal_cents' => $quote->subtotalCents,
                'tax_cents' => $quote->taxCents,
                'total_cents' => $quote->totalCents,
                'currency' => $quote->currency,
                'hold_expires_at' => $held ? $now->modify("+{$settings->holdMinutes} minutes")->format('Y-m-d H:i:s') : null,
                'created_by' => $rules->createdBy,
                'created_at' => $timestamp,
                'updated_at' => $timestamp,
                'request_id' => $requestId,
            ], $chosen);

            $reservation = $this->reservations->find($id, $tz);
            if ($reservation === null) {
                throw new \LogicException('Reservation vanished after insert.');
            }

            return $reservation;
        });
    }

    /**
     * Staff edit of a held or confirmed reservation. Contact fields always update. When the date,
     * start, duration or station count change, the new slot is checked under the day lock exactly
     * like a new booking (excluding this reservation), and the amounts are recomputed when the
     * duration or station count changed; staff settle any difference at the counter. Status is kept.
     *
     * @throws BookingRejected
     */
    public function update(int $id, BookingRequest $request, BookingRules $rules): Reservation
    {
        $errors = $request->validate($rules->contactRequired);
        if ($errors !== []) {
            throw new BookingRejected('validation_failed', $errors);
        }
        $settings = $this->settings->load();
        $tz = $settings->tz();
        $now = $this->clock->now();

        $existing = $this->reservations->find($id, $tz);
        if ($existing === null) {
            throw new BookingRejected('not_found');
        }
        if (!in_array($existing->status, ['held', 'confirmed'], true)) {
            throw new BookingRejected('wrong_status');
        }

        $scheduleChanged = $existing->localDate !== $request->localDate
            || $existing->startMinute !== $request->startMinute
            || $existing->durationMinutes !== $request->durationMinutes
            || count($existing->stationIds) !== $request->stationCount;

        if (!$scheduleChanged) {
            $this->reservations->updateContact($id, trim($request->firstName), trim($request->lastName), trim($request->email), trim($request->phone), $request->comments === null ? null : trim($request->comments), $now);

            return $this->reservations->find($id, $tz) ?? throw new \LogicException('Reservation vanished after update.');
        }

        $hours = $this->checkRules($request, $rules, $settings, $now->setTimezone($tz));
        $numbersById = $this->stations->activeNumbersById();
        if ($request->stationCount < 1 || $request->stationCount > count($numbersById)) {
            throw new BookingRejected('invalid_station_count');
        }

        $amountsChange = $existing->durationMinutes !== $request->durationMinutes
            || count($existing->stationIds) !== $request->stationCount;
        if ($rules->complimentary || ($existing->totalCents === 0 && !$amountsChange)) {
            $quote = new Quote(0, 0, 0, $settings->currency);
        } elseif ($amountsChange) {
            $quote = $this->quote($request, $settings);
        } else {
            $quote = new Quote($existing->subtotalCents, $existing->taxCents, $existing->totalCents, $existing->currency);
        }

        [$startUtc, $endUtc] = self::utcRange($request, $tz);
        $endMinute = $request->startMinute + $request->durationMinutes;
        $openMinute = $hours === null ? 0 : $hours->openMinute;

        // Lock both the old and the new date, always in the same order, so two staff edits cannot deadlock.
        $dates = array_values(array_unique([$existing->localDate, $request->localDate]));
        sort($dates);
        foreach ($dates as $date) {
            $this->reservations->ensureDayRow($date);
        }

        return Transaction::run($this->pdo, function () use ($id, $dates, $request, $settings, $tz, $now, $numbersById, $quote, $startUtc, $endUtc, $endMinute, $openMinute): Reservation {
            foreach ($dates as $date) {
                $this->reservations->lockDay($date);
            }
            $current = $this->reservations->find($id, $tz, true);
            if ($current === null || !in_array($current->status, ['held', 'confirmed'], true)) {
                throw new BookingRejected('wrong_status');
            }
            $blocks = $this->reservations->blocksForDate($request->localDate, $now, $tz, $id);
            $free = Availability::freeStations(array_keys($numbersById), $blocks, $request->startMinute, $endMinute, $settings->bufferMinutes);
            $chosen = StationAllocator::choose($free, $blocks, $numbersById, $request->startMinute, $request->stationCount, $openMinute);
            if ($chosen === null) {
                throw new BookingRejected('slot_unavailable');
            }
            $this->reservations->updateContact($id, trim($request->firstName), trim($request->lastName), trim($request->email), trim($request->phone), $request->comments === null ? null : trim($request->comments), $now);
            $this->reservations->updateSchedule($id, $request->localDate, $startUtc, $endUtc, $request->durationMinutes, $request->stationCount, $quote, $chosen, $now);

            return $this->reservations->find($id, $tz) ?? throw new \LogicException('Reservation vanished after update.');
        });
    }

    /**
     * Marks a held reservation paid. Safe to call twice with the same payment id.
     * A past-due hold is honoured the same way no matter whether expireHolds() already swept it:
     * whether the row is still "held" with an old deadline, or already flipped to "expired", the
     * outcome depends only on whether its stations are still free. If they are, the payment is
     * accepted. If not, the reservation becomes (or stays) "expired" and BookingRejected('hold_expired')
     * tells the caller to refund.
     *
     * @throws BookingRejected
     */
    public function confirmPayment(int $id, string $provider, string $paymentId): Reservation
    {
        $settings = $this->settings->load();
        $tz = $settings->tz();
        $existing = $this->reservations->find($id, $tz);
        if ($existing === null) {
            throw new BookingRejected('not_found');
        }
        $this->reservations->ensureDayRow($existing->localDate);

        $result = Transaction::run($this->pdo, function () use ($id, $provider, $paymentId, $settings, $tz, $existing): Reservation|string {
            $this->reservations->lockDay($existing->localDate);
            $now = $this->clock->now();
            $current = $this->reservations->find($id, $tz, true);
            if ($current === null) {
                throw new BookingRejected('not_found');
            }
            if ($current->status === 'confirmed' && $current->paymentId === $paymentId) {
                return $current;
            }
            // A hold can be past due in two ways: still "held" with an old deadline, or already swept to
            // "expired" by expireHolds(). Both are handled the same, so the outcome never depends on
            // whether the sweep happened to run first.
            if (!in_array($current->status, ['held', 'expired'], true)) {
                throw new BookingRejected('wrong_status');
            }
            $expired = $current->status === 'expired'
                || ($current->holdExpiresAtUtc !== null && $current->holdExpiresAtUtc <= $now->format('Y-m-d H:i:s'));
            if ($expired) {
                $blocks = $this->reservations->blocksForDate($current->localDate, $now, $tz, $id);
                $free = Availability::freeStations(
                    $current->stationIds,
                    $blocks,
                    $current->startMinute,
                    $current->startMinute + $current->durationMinutes,
                    $settings->bufferMinutes
                );
                if (count($free) !== count($current->stationIds)) {
                    if ($current->status === 'held') {
                        $this->reservations->transition($id, ['held'], 'expired', $now);
                    }

                    return 'hold_expired';
                }
            }
            $this->reservations->transition($id, ['held', 'expired'], 'confirmed', $now, $provider, $paymentId);
            $confirmed = $this->reservations->find($id, $tz);
            if ($confirmed === null) {
                throw new \LogicException('Reservation vanished after confirm.');
            }

            return $confirmed;
        });

        if (is_string($result)) {
            throw new BookingRejected($result);
        }

        return $result;
    }

    public function failPayment(int $id): void
    {
        $this->reservations->transition($id, ['held'], 'payment_failed', $this->clock->now());
    }

    /** @throws BookingRejected */
    public function cancel(int $id): void
    {
        if (!$this->reservations->transition($id, ['held', 'confirmed'], 'cancelled', $this->clock->now())) {
            throw new BookingRejected('wrong_status');
        }
    }

    public function expireHolds(): int
    {
        return Transaction::run($this->pdo, fn (): int => $this->reservations->expireHolds($this->clock->now()));
    }

    /**
     * Releases overdue holds. With a real payment gateway each hold is first checked for a payment
     * that completed after its response was lost: found and stations still free -> confirmed;
     * found but stations taken -> refunded; positively not found -> expired. A payment the provider still
     * reports as pending keeps the hold for up to a day; a lookup that fails keeps it until one succeeds.
     *
     * @return array{confirmed:int,expired:int,refunded:int,kept:int}
     */
    public function reconcileHolds(PaymentGateway $gateway, Logger $logger): array
    {
        $report = ['confirmed' => 0, 'expired' => 0, 'refunded' => 0, 'kept' => 0];
        if ($gateway->mode() === 'none') {
            $report['expired'] = $this->expireHolds();

            return $report;
        }
        $now = $this->clock->now();
        foreach ($this->reservations->overdueHolds($now) as $hold) {
            try {
                $payment = $gateway->findByReference($hold['code'], $hold['createdAt']);
            } catch (PaymentLookupFailed $error) {
                // Not knowing is not the same as "no payment": keep the hold and ask again next run.
                $logger->warning('payment lookup failed; hold kept', ['reservation' => $hold['id'], 'error' => $error->getMessage()]);
                $report['kept']++;

                continue;
            }
            if ($payment === null || $payment->outcome !== PaymentResult::PAID) {
                $stale = $hold['holdExpiresAt'] <= $now->modify('-1 day');
                if ($payment !== null && !$stale) {
                    $report['kept']++;
                    $logger->warning('payment still pending at the provider; hold kept', ['reservation' => $hold['id']]);
                    continue;
                }
                if ($this->reservations->transition($hold['id'], ['held'], 'expired', $now)) {
                    $report['expired']++;
                }
                continue;
            }
            try {
                $this->confirmPayment($hold['id'], $gateway->mode(), (string) $payment->paymentId);
                $logger->info('late payment reconciled', ['reservation' => $hold['id']]);
                $report['confirmed']++;
            } catch (BookingRejected $rejected) {
                if ($rejected->reason !== 'hold_expired') {
                    $logger->warning('could not reconcile payment', ['reservation' => $hold['id'], 'reason' => $rejected->reason]);
                    continue;
                }
                $row = $this->reservations->find($hold['id'], $this->settings->load()->tz());
                $refunded = $row !== null && $gateway->refund((string) $payment->paymentId, $row->totalCents, $row->currency, 'Time no longer available');
                $logger->error('late payment for a lost slot', ['reservation' => $hold['id'], 'refunded' => $refunded]);
                $report['refunded']++;
            }
        }

        return $report;
    }

    /**
     * Checks the rules the caller enforces and returns the day's opening hours (null when closed
     * and the rules allow booking anyway).
     *
     * @throws BookingRejected
     */
    private function checkRules(BookingRequest $request, BookingRules $rules, VenueSettings $settings, \DateTimeImmutable $nowLocal): ?DayHours
    {
        $today = $nowLocal->format('Y-m-d');
        if ($request->localDate < $today) {
            throw new BookingRejected('date_in_past');
        }
        if ($rules->enforceAdvanceLimit) {
            $lastDate = $nowLocal->modify("+{$settings->maxAdvanceDays} days")->format('Y-m-d');
            if ($request->localDate > $lastDate) {
                throw new BookingRejected('too_far_ahead');
            }
        }
        $endMinute = $request->startMinute + $request->durationMinutes;
        if ($endMinute > 1440) {
            throw new BookingRejected('outside_hours');
        }
        $hours = $this->hours->forDate($request->localDate);
        if ($rules->enforceHours) {
            if ($hours === null) {
                throw new BookingRejected('closed');
            }
            if ($request->startMinute < $hours->openMinute || $endMinute > $hours->closeMinute) {
                throw new BookingRejected('outside_hours');
            }
        }
        if ($rules->enforceGrid && $hours !== null
            && ($request->startMinute - $hours->openMinute) % $settings->slotStepMinutes !== 0) {
            throw new BookingRejected('off_grid');
        }
        if ($rules->enforceLeadTime && $request->localDate === $today) {
            $nowMinute = (int) $nowLocal->format('G') * 60 + (int) $nowLocal->format('i');
            if ($request->startMinute < $nowMinute + $settings->minLeadMinutes) {
                throw new BookingRejected('too_soon');
            }
        }

        return $hours;
    }

    /** @throws BookingRejected */
    private function quote(BookingRequest $request, VenueSettings $settings): Quote
    {
        $weekday = (int) (new \DateTimeImmutable($request->localDate))->format('w');
        $priceList = $this->prices->load();
        if ($priceList->priceCents($weekday, $request->durationMinutes) === null) {
            throw new BookingRejected('duration_not_offered');
        }

        return $priceList->quote($weekday, $request->durationMinutes, $request->stationCount, $settings->taxRateBp, $settings->currency);
    }

    /**
     * Wall-clock local time -> UTC strings. setTime() keeps this correct on daylight-saving change days.
     *
     * @return array{0:string,1:string}
     */
    private static function utcRange(BookingRequest $request, \DateTimeZone $tz): array
    {
        $utc = new \DateTimeZone('UTC');
        $endMinute = $request->startMinute + $request->durationMinutes;
        $midnight = new \DateTimeImmutable($request->localDate . ' 00:00:00', $tz);
        $start = $midnight->setTime(intdiv($request->startMinute, 60), $request->startMinute % 60)->setTimezone($utc);
        $end = $midnight->setTime(intdiv($endMinute, 60), $endMinute % 60)->setTimezone($utc);

        return [$start->format('Y-m-d H:i:s'), $end->format('Y-m-d H:i:s')];
    }
}
