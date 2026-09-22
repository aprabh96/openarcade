<?php

declare(strict_types=1);

namespace ArcadeOS\Payments;

interface PaymentGateway
{
    /** "none" or "square"; tells the booking page which flow to run. */
    public function mode(): string;

    /**
     * Charges the customer for a held reservation.
     *
     * @param string $sourceToken the card token produced by the payment provider's browser SDK
     * @param string $idempotencyKey the reservation uuid; the same key must never charge twice
     */
    public function charge(int $amountCents, string $currency, string $sourceToken, string $idempotencyKey, string $referenceCode): PaymentResult;

    /** Looks a payment up by the idempotency key used to create it, for reconciliation. */
    public function find(string $idempotencyKey): ?PaymentResult;

    /** Refunds a payment in full. Returns false when the provider refused or could not be reached. */
    public function refund(string $paymentId, int $amountCents, string $currency, string $reason): bool;
}
