<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

use ArcadeOS\Http\ApiError;
use ArcadeOS\Mail\Mailer;
use ArcadeOS\Mail\ReservationMail;
use ArcadeOS\Payments\PaymentGateway;
use ArcadeOS\Payments\PaymentResult;
use ArcadeOS\Settings\SettingsRepository;
use ArcadeOS\Support\Logger;

/** A customer booking from start to finish: reserve, charge when required, confirm, notify. */
final class BookingFlow
{
    public function __construct(
        private Reservations $reservations,
        private ReservationRepository $repository,
        private SettingsRepository $settings,
        private PaymentGateway $gateway,
        private Mailer $mailer,
        private Logger $logger,
    ) {
    }

    /**
     * @param string|null $requestId client-chosen id for this attempt; a retry with the same id
     *                               returns the first reservation and is never charged twice
     * @return array<string,mixed> the finished reservation row
     * @throws BookingRejected|ApiError
     */
    public function book(BookingRequest $request, ?string $paymentToken, ?string $requestId = null): array
    {
        $requirePayment = $this->gateway->mode() !== 'none';
        $errors = $request->validate(true);
        if ($requirePayment && ($paymentToken === null || $paymentToken === '')) {
            $errors['payment_token'] = 'Payment details are required.';
        }
        if ($errors !== []) {
            throw new BookingRejected('validation_failed', $errors);
        }

        $venue = $this->settings->load();
        if ($requestId !== null && ($existingId = $this->repository->idForRequest($requestId)) !== null) {
            return $this->repeat($existingId, $venue);
        }
        try {
            $reservation = $this->reservations->create($request, BookingRules::customer($requirePayment), $requestId);
        } catch (DuplicateRequest) {
            return $this->repeat((int) $this->repository->idForRequest((string) $requestId), $venue);
        }
        if ($reservation->status === 'held') {
            $reservation = $this->charge($reservation, (string) $paymentToken);
        }

        $row = $this->repository->findRow($reservation->id, $venue->tz());
        if ($row === null) {
            throw new \LogicException('Reservation vanished after booking.');
        }
        $this->notify($row, $venue);

        return $row;
    }

    /**
     * A retried submission. A finished booking is returned as it is; one whose payment is still
     * being settled is reported as such and never charged again.
     *
     * @return array<string,mixed>
     */
    private function repeat(int $id, \ArcadeOS\Settings\VenueSettings $venue): array
    {
        $row = $this->repository->findRow($id, $venue->tz());
        if ($row === null) {
            throw new \LogicException('Reservation vanished.');
        }
        if ($row['status'] === 'confirmed') {
            return $row;
        }
        if ($row['status'] === 'held') {
            throw self::paymentUnknown();
        }
        throw ApiError::conflict('request_used', 'That booking attempt already finished. Please start again.');
    }

    private static function paymentUnknown(): ApiError
    {
        return new ApiError(
            'payment_unknown',
            'We could not confirm the payment. Please do not try again for a few minutes. If your card was charged, the booking will be completed automatically.',
            503,
        );
    }

    /** @throws ApiError */
    private function charge(Reservation $reservation, string $paymentToken): Reservation
    {
        $result = $this->gateway->charge(
            $reservation->totalCents,
            $reservation->currency,
            $paymentToken,
            $reservation->uuid,
            $reservation->confirmationCode,
        );
        if ($result->outcome === PaymentResult::DECLINED) {
            $this->reservations->failPayment($reservation->id);
            $this->logger->info('payment declined', ['reservation' => $reservation->id]);
            throw new ApiError('payment_declined', $result->message !== '' ? $result->message : 'The card was declined.', 402);
        }
        if ($result->outcome === PaymentResult::ERROR) {
            $this->reservations->failPayment($reservation->id);
            $this->logger->error('payment provider refused the request; check the Square settings', ['reservation' => $reservation->id, 'detail' => $result->message]);
            throw new ApiError('payment_unavailable', 'Online payment is not working right now and your card was not charged. Please call the venue to book.', 503);
        }
        if ($result->outcome === PaymentResult::UNKNOWN) {
            $this->logger->warning('payment outcome unknown; hold kept for reconciliation', ['reservation' => $reservation->id]);
            throw self::paymentUnknown();
        }
        try {
            return $this->reservations->confirmPayment($reservation->id, $this->gateway->mode(), (string) $result->paymentId);
        } catch (BookingRejected $rejected) {
            // The card was charged but the booking cannot stand (the hold lapsed and the time was taken,
            // or staff cancelled it meanwhile). Whatever the reason, give the money back.
            $refunded = $this->gateway->refund((string) $result->paymentId, $reservation->totalCents, $reservation->currency, 'Booking could not be completed');
            $this->logger->error('paid but booking could not be confirmed', ['reservation' => $reservation->id, 'reason' => $rejected->reason, 'refunded' => $refunded]);
            throw ApiError::conflict(
                'slot_unavailable',
                $refunded
                    ? 'That time was taken while your payment went through. The charge has been refunded; please choose another time.'
                    : 'That time was taken while your payment went through. Your charge will be refunded by the venue; please choose another time.',
            );
        }
    }

    /** @param array<string,mixed> $row */
    private function notify(array $row, \ArcadeOS\Settings\VenueSettings $venue): void
    {
        $customer = ReservationMail::customer($row, $venue);
        if (!$this->mailer->send((string) $row['email'], trim($row['first_name'] . ' ' . $row['last_name']), $customer['subject'], $customer['text'])) {
            $this->logger->warning('customer confirmation email failed', ['reservation' => $row['id']]);
        }
        if ($venue->notificationEmail !== '') {
            $staff = ReservationMail::venue($row, $venue);
            if (!$this->mailer->send($venue->notificationEmail, $venue->venueName, $staff['subject'], $staff['text'])) {
                $this->logger->warning('venue notification email failed', ['reservation' => $row['id']]);
            }
        }
    }
}
