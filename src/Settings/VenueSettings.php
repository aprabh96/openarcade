<?php

declare(strict_types=1);

namespace ArcadeOS\Settings;

final class VenueSettings
{
    public const DEFAULTS = [
        'venue_name' => 'My VR Arcade',
        'timezone' => 'America/Chicago',
        'currency' => 'USD',
        'tax_rate_bp' => '0',
        'slot_step_minutes' => '30',
        'buffer_minutes' => '10',
        'min_lead_minutes' => '30',
        'max_advance_days' => '90',
        'hold_minutes' => '10',
        'notification_email' => '',
    ];

    private function __construct(
        public readonly string $venueName,
        public readonly string $timezone,
        public readonly string $currency,
        public readonly int $taxRateBp,
        public readonly int $slotStepMinutes,
        public readonly int $bufferMinutes,
        public readonly int $minLeadMinutes,
        public readonly int $maxAdvanceDays,
        public readonly int $holdMinutes,
        public readonly string $notificationEmail,
    ) {
    }

    /** @param array<string,string> $values */
    public static function fromArray(array $values): self
    {
        $v = array_merge(self::DEFAULTS, array_intersect_key($values, self::DEFAULTS));

        $name = trim($v['venue_name']);
        if ($name === '' || mb_strlen($name) > 80) {
            throw new \InvalidArgumentException('venue_name must be 1-80 characters.');
        }
        if (!in_array($v['timezone'], \DateTimeZone::listIdentifiers(), true)) {
            throw new \InvalidArgumentException('timezone must be a valid IANA timezone, for example America/Chicago.');
        }
        if (preg_match('/^[A-Z]{3}$/', $v['currency']) !== 1) {
            throw new \InvalidArgumentException('currency must be a 3-letter upper-case code.');
        }
        $email = trim($v['notification_email']);
        if ($email !== '' && filter_var($email, FILTER_VALIDATE_EMAIL) === false) {
            throw new \InvalidArgumentException('notification_email is not a valid email address.');
        }

        return new self(
            $name,
            $v['timezone'],
            $v['currency'],
            self::intInRange($v, 'tax_rate_bp', 0, 10000),
            self::intInRange($v, 'slot_step_minutes', 5, 240),
            self::intInRange($v, 'buffer_minutes', 0, 240),
            self::intInRange($v, 'min_lead_minutes', 0, 10080),
            self::intInRange($v, 'max_advance_days', 1, 730),
            self::intInRange($v, 'hold_minutes', 1, 120),
            $email,
        );
    }

    public function tz(): \DateTimeZone
    {
        return new \DateTimeZone($this->timezone);
    }

    /** @param array<string,string> $values */
    private static function intInRange(array $values, string $key, int $min, int $max): int
    {
        $raw = $values[$key];
        if (preg_match('/^-?\d+$/', $raw) !== 1 || (int) $raw < $min || (int) $raw > $max) {
            throw new \InvalidArgumentException("{$key} must be a whole number from {$min} to {$max}.");
        }

        return (int) $raw;
    }
}
