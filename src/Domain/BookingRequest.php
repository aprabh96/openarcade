<?php

declare(strict_types=1);

namespace ArcadeOS\Domain;

final class BookingRequest
{
    public function __construct(
        public readonly string $localDate,
        public readonly int $startMinute,
        public readonly int $durationMinutes,
        public readonly int $stationCount,
        public readonly string $firstName,
        public readonly string $lastName,
        public readonly string $email,
        public readonly string $phone,
        public readonly ?string $comments = null,
    ) {
    }

    /** @return array<string,string> field => problem; empty when valid */
    public function validate(bool $contactRequired): array
    {
        $errors = [];
        $date = \DateTimeImmutable::createFromFormat('!Y-m-d', $this->localDate);
        if ($date === false || $date->format('Y-m-d') !== $this->localDate) {
            $errors['date'] = 'Date must be YYYY-MM-DD.';
        }
        if ($this->startMinute < 0 || $this->startMinute > 1439) {
            $errors['start'] = 'Start time is not valid.';
        }
        if ($this->durationMinutes < 5 || $this->durationMinutes > 1440) {
            $errors['duration'] = 'Duration is not valid.';
        }
        foreach (['first_name' => $this->firstName, 'last_name' => $this->lastName] as $field => $value) {
            $length = mb_strlen(trim($value));
            if ($length < 1 || $length > 60 || self::hasControlCharacters($value)) {
                $errors[$field] = 'Must be 1 to 60 characters.';
            }
        }
        $email = trim($this->email);
        $emailBad = $email === ''
            ? $contactRequired
            : (mb_strlen($email) > 190 || filter_var($email, FILTER_VALIDATE_EMAIL) === false);
        if ($emailBad) {
            $errors['email'] = 'Enter a valid email address.';
        }
        $phone = trim($this->phone);
        $phoneBad = $phone === '' ? $contactRequired : preg_match('/^[0-9+()\-. ]{7,30}$/', $phone) !== 1;
        if ($phoneBad) {
            $errors['phone'] = 'Enter a valid phone number.';
        }
        if ($this->comments !== null && (mb_strlen($this->comments) > 1000 || self::hasControlCharacters($this->comments, true))) {
            $errors['comments'] = 'Must be 1000 characters or fewer.';
        }

        return $errors;
    }

    /** Line breaks and tabs are allowed only where $multiline says so; other control characters never. */
    private static function hasControlCharacters(string $value, bool $multiline = false): bool
    {
        $pattern = $multiline ? '/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/' : '/[\x00-\x1F\x7F]/';

        return preg_match($pattern, $value) === 1;
    }
}
