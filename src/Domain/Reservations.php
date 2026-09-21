<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

use ArcadeOS\Db\Transaction;
use ArcadeOS\Settings\SettingsRepository;
use ArcadeOS\Support\Clock;
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
    public function create(BookingRequest $request, BookingRules $rules): Reservation
    {
        $errors = $request->validate($rules->contactRequired);
        if ($errors !== []) {
            throw new BookingRejected('validation_failed', $errors);
        }

        $settings = $this->settings->load();
        $tz = $settings->tz();
        $now = $this->clock->now();
        $nowLocal = $now->setTimezone($tz);
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

        $numbersById = $this->stations->activeNumbersById();
        if ($request->stationCount < 1 || $request->stationCount > count($numbersById)) {
            throw new BookingRejected('invalid_station_count');
        }

        if ($rules->complimentary) {
            $quote = new Quote(0, 0, 0, $settings->currency);
        } else {
            $weekday = (int) (new \DateTimeImmutable($request->localDate))->format('w');
            $priceList = $this->prices->load();
            if ($priceList->priceCents($weekday, $request->durationMinutes) === null) {
                throw new BookingRejected('duration_not_offered');
            }
            $quote = $priceList->quote($weekday, $request->durationMinutes, $request->stationCount, $settings->taxRateBp, $settings->currency);
        }

        // Wall-clock local time -> UTC. setTime() keeps this correct on daylight-saving change days.
        $utc = new \DateTimeZone('UTC');
        $midnight = new \DateTimeImmutable($request->localDate . ' 00:00:00', $tz);
        $startUtc = $midnight->setTime(intdiv($request->startMinute, 60), $request->startMinute % 60)->setTimezone($utc);
        $endUtc = $midnight->setTime(intdiv($endMinute, 60), $endMinute % 60)->setTimezone($utc);

        $held = $rules->requirePayment && $quote->totalCents > 0;
        $openMinute = $hours === null ? 0 : $hours->openMinute;

        $this->reservations->ensureDayRow($request->localDate);

        return Transaction::run($this->pdo, function () use ($request, $rules, $settings, $tz, $now, $numbersById, $quote, $startUtc, $endUtc, $endMinute, $held, $openMinute): Reservation {
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
                'start_utc' => $startUtc->format('Y-m-d H:i:s'),
                'end_utc' => $endUtc->format('Y-m-d H:i:s'),
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
            ], $chosen);

            $reservation = $this->reservations->find($id, $tz);
            if ($reservation === null) {
                throw new \LogicException('Reservation vanished after insert.');
            }

            return $reservation;
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
}
