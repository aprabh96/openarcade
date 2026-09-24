<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Support;

use OpenArcade\Payments\PaymentGateway;
use OpenArcade\Payments\PaymentLookupFailed;
use OpenArcade\Payments\PaymentResult;

/** A scripted "square" gateway: the next charge produces whatever the test queued. */
final class FakeGateway implements PaymentGateway
{
    /** @var PaymentResult[] */
    private array $charges = [];

    /** @var array<int, array<string,mixed>> */
    public array $chargeCalls = [];

    /** @var array<int, array<string,mixed>> */
    public array $refunds = [];

    public bool $refundSucceeds = true;

    public ?PaymentResult $lookup = null;

    /** When true, findByReference() throws as if Square could not be reached. */
    public bool $lookupFails = false;

    public int $lookups = 0;

    public function mode(): string
    {
        return 'square';
    }

    public function willReturn(PaymentResult $result): self
    {
        $this->charges[] = $result;

        return $this;
    }

    public function charge(int $amountCents, string $currency, string $sourceToken, string $idempotencyKey, string $referenceCode): PaymentResult
    {
        $this->chargeCalls[] = compact('amountCents', 'currency', 'sourceToken', 'idempotencyKey', 'referenceCode');
        $next = array_shift($this->charges);
        if ($next === null) {
            throw new \LogicException('FakeGateway: no charge result queued');
        }

        return $next;
    }

    public function findByReference(string $referenceCode, \DateTimeImmutable $notBefore): ?PaymentResult
    {
        $this->lookups++;
        if ($this->lookupFails) {
            throw new PaymentLookupFailed('FakeGateway: lookup failed');
        }

        return $this->lookup;
    }

    public function refund(string $paymentId, int $amountCents, string $currency, string $reason): bool
    {
        $this->refunds[] = compact('paymentId', 'amountCents', 'currency', 'reason');

        return $this->refundSucceeds;
    }
}
