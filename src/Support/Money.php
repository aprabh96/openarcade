<?php

declare(strict_types=1);

namespace OpenArcade\Support;

final class Money
{
    private function __construct(public readonly int $cents, public readonly string $currency)
    {
    }

    public static function of(int $cents, string $currency): self
    {
        if ($cents < 0) {
            throw new \InvalidArgumentException('Money cannot be negative.');
        }
        if (preg_match('/^[A-Z]{3}$/', $currency) !== 1) {
            throw new \InvalidArgumentException('Currency must be a 3-letter upper-case code.');
        }

        return new self($cents, $currency);
    }

    public function times(int $quantity): self
    {
        return self::of($this->cents * $quantity, $this->currency);
    }

    public function plus(self $other): self
    {
        if ($other->currency !== $this->currency) {
            throw new \InvalidArgumentException('Cannot add different currencies.');
        }

        return self::of($this->cents + $other->cents, $this->currency);
    }

    /** Tax at a rate in basis points (935 = 9.35%), rounded half up to the cent. */
    public function taxAt(int $basisPoints): self
    {
        if ($basisPoints < 0) {
            throw new \InvalidArgumentException('Tax rate cannot be negative.');
        }

        return self::of(intdiv($this->cents * $basisPoints + 5000, 10000), $this->currency);
    }

    public function format(): string
    {
        return sprintf('%s %d.%02d', $this->currency, intdiv($this->cents, 100), $this->cents % 100);
    }
}
