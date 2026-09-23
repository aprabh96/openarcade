<?php

declare(strict_types=1);

namespace ArcadeOS\Payments;

final class PaymentResult
{
    public const PAID = 'paid';
    public const DECLINED = 'declined';
    public const UNKNOWN = 'unknown';
    public const ERROR = 'error';

    private function __construct(
        public readonly string $outcome,
        public readonly ?string $paymentId,
        public readonly string $message,
    ) {
    }

    public static function paid(string $paymentId): self
    {
        return new self(self::PAID, $paymentId, '');
    }

    /** The card was refused; nothing was charged. */
    public static function declined(string $message): self
    {
        return new self(self::DECLINED, null, $message);
    }

    /** The provider did not answer clearly; the charge may or may not exist. */
    public static function unknown(string $message): self
    {
        return new self(self::UNKNOWN, null, $message);
    }

    /** The provider rejected the request itself (bad credentials, wrong location); nothing was charged. */
    public static function error(string $message): self
    {
        return new self(self::ERROR, null, $message);
    }
}
