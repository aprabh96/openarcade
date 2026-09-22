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
     * @return array<string,mixed> the finished reservation row
     * @throws BookingRejected|ApiError
     */
    public function book(BookingRequest $request, ?string $paymentToken): array
    {
        $requirePayment = $this->gateway->mode() !== 'none';
        $errors = $request->validate(true);
        if ($requirePayment && ($paymentToken === null || $paymentToken === '')) {
            $errors['payment_token'] = 'Payment details are required.';
        }
        if ($errors !== []) {
            throw new BookingRejected('validation_failed', $errors);
        }

        $reservation = $this->reservations->create($request, BookingRules::customer($requirePayment));
        if ($reservation->status === 'held') {
            $reservation = $this->charge($reservation, (string) $paymentToken);
        }

        $venue = $this->settings->load();
        $row = $this->repository->findRow($reservation->id, $venue->tz());
        if ($row === null) {
            throw new \LogicException('Reservation vanished after booking.');
        }
        $this->notify($row, $venue);

        return $row;
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
        if ($result->outcome === PaymentResult::UNKNOWN) {
            $this->logger->warning('payment outcome unknown; hold kept for reconciliation', ['reservation' => $reservation->id]);
            throw new ApiError(
                'payment_unknown',
                'We could not confirm the payment. Please do not try again for a few minutes. If your card was charged, the booking will be completed automatically.',
                503,
            );
        }
        try {
            return $this->reservations->confirmPayment($reservation->id, $this->gateway->mode(), (string) $result->paymentId);
        } catch (BookingRejected $rejected) {
            if ($rejected->reason !== 'hold_expired') {
                throw $rejected;
            }
            $refunded = $this->gateway->refund((string) $result->paymentId, $reservation->totalCents, $reservation->currency, 'Time no longer available');
            $this->logger->error('paid but slot lost after hold expiry', ['reservation' => $reservation->id, 'refunded' => $refunded]);
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
