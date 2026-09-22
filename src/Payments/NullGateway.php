<?php

declare(strict_types=1);

namespace ArcadeOS\Payments;

/** Payment mode "none": customers pay at the venue, so nothing is ever charged. */
final class NullGateway implements PaymentGateway
{
    public function mode(): string
    {
        return 'none';
    }

    public function charge(int $amountCents, string $currency, string $sourceToken, string $idempotencyKey, string $referenceCode): PaymentResult
    {
        throw new \LogicException('Payment mode "none" never charges.');
    }

    public function findByReference(string $referenceCode, \DateTimeImmutable $notBefore): ?PaymentResult
    {
        return null;
    }

    public function refund(string $paymentId, int $amountCents, string $currency, string $reason): bool
    {
        return false;
    }
}
