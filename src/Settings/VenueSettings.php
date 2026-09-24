<?php

declare(strict_types=1);

namespace OpenArcade\Settings;

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
        'brand_color' => '#1f2937',
        'logo_url' => '',
        'venue_address' => '',
        'venue_phone' => '',
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
        public readonly string $brandColor,
        public readonly string $logoUrl,
        public readonly string $venueAddress,
        public readonly string $venuePhone,
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
        $color = strtolower(trim($v['brand_color']));
        if (preg_match('/^#[0-9a-f]{6}$/', $color) !== 1) {
            throw new \InvalidArgumentException('brand_color must be a hex colour such as #1f2937.');
        }
        $logo = trim($v['logo_url']);
        if ($logo !== '' && (mb_strlen($logo) > 300 || preg_match('#^https://[^\s"\'<>]+$#', $logo) !== 1)) {
            throw new \InvalidArgumentException('logo_url must be an https URL.');
        }
        $address = trim($v['venue_address']);
        if (mb_strlen($address) > 200) {
            throw new \InvalidArgumentException('venue_address must be 200 characters or fewer.');
        }
        $phone = trim($v['venue_phone']);
        if ($phone !== '' && preg_match('/^[0-9+()\-. ]{7,30}$/', $phone) !== 1) {
            throw new \InvalidArgumentException('venue_phone is not a valid phone number.');
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
            $color,
            $logo,
            $address,
            $phone,
        );
    }

    public function tz(): \DateTimeZone
    {
        return new \DateTimeZone($this->timezone);
    }

    /**
     * Everything staff may edit, as strings, for the settings screen.
     *
     * @return array<string,string>
     */
    public function toArray(): array
    {
        return [
            'venue_name' => $this->venueName,
            'timezone' => $this->timezone,
            'currency' => $this->currency,
            'tax_rate_bp' => (string) $this->taxRateBp,
            'slot_step_minutes' => (string) $this->slotStepMinutes,
            'buffer_minutes' => (string) $this->bufferMinutes,
            'min_lead_minutes' => (string) $this->minLeadMinutes,
            'max_advance_days' => (string) $this->maxAdvanceDays,
            'hold_minutes' => (string) $this->holdMinutes,
            'notification_email' => $this->notificationEmail,
            'brand_color' => $this->brandColor,
            'logo_url' => $this->logoUrl,
            'venue_address' => $this->venueAddress,
            'venue_phone' => $this->venuePhone,
        ];
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
