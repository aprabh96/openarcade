<?php

declare(strict_types=1);

namespace OpenArcade\Payments;

interface PaymentGateway
{
    /** "none" or "square"; tells the booking page which flow to run. */
    public function mode(): string;

    /**
     * Charges the customer for a held reservation. Never throws: transport problems come back as
     * an "unknown" result so the caller can keep the hold for reconciliation.
     *
     * @param string $sourceToken the card token produced by the payment provider's browser SDK
     * @param string $idempotencyKey the reservation uuid; the same key must never charge twice
     * @param string $referenceCode the confirmation code, stored with the payment so it can be found again
     */
    public function charge(int $amountCents, string $currency, string $sourceToken, string $idempotencyKey, string $referenceCode): PaymentResult;

    /**
     * Finds a payment by the reference code it was created with, for reconciling a charge whose
     * response never arrived. Returns null only when the provider positively reports no such payment.
     *
     * @throws PaymentLookupFailed when the provider could not be asked (network or HTTP error)
     */
    public function findByReference(string $referenceCode, \DateTimeImmutable $notBefore): ?PaymentResult;

    /** Refunds a payment in full. Returns false when the provider refused or could not be reached. */
    public function refund(string $paymentId, int $amountCents, string $currency, string $reason): bool;
}
