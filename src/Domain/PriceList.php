<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

use ArcadeOS\Support\Money;

final class PriceList
{
    public const EVERY_DAY = -1;

    /** @param array<int, array<int,int>> $centsByWeekday weekday (-1 = every day, 0..6) => [duration minutes => cents per station] */
    public function __construct(private array $centsByWeekday)
    {
    }

    public function priceCents(int $weekday, int $durationMinutes): ?int
    {
        return $this->centsByWeekday[$weekday][$durationMinutes]
            ?? $this->centsByWeekday[self::EVERY_DAY][$durationMinutes]
            ?? null;
    }

    /** @return int[] durations offered on that weekday, ascending */
    public function durations(int $weekday): array
    {
        $durations = array_keys(
            ($this->centsByWeekday[self::EVERY_DAY] ?? []) + ($this->centsByWeekday[$weekday] ?? [])
        );
        sort($durations);

        return $durations;
    }

    public function quote(int $weekday, int $durationMinutes, int $stationCount, int $taxRateBp, string $currency): Quote
    {
        $price = $this->priceCents($weekday, $durationMinutes);
        if ($price === null) {
            throw new \InvalidArgumentException('Duration is not offered on that day.');
        }
        if ($stationCount < 1) {
            throw new \InvalidArgumentException('Station count must be at least 1.');
        }
        $subtotal = Money::of($price, $currency)->times($stationCount);
        $tax = $subtotal->taxAt($taxRateBp);

        return new Quote($subtotal->cents, $tax->cents, $subtotal->plus($tax)->cents, $currency);
    }
}
