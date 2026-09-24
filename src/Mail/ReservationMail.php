<?php

declare(strict_types=1);

namespace OpenArcade\Mail;

use OpenArcade\Settings\VenueSettings;

/** Plain-text confirmation emails built from a reservation row (ReservationRepository::findRow). */
final class ReservationMail
{
    /**
     * @param array<string,mixed> $row
     * @return array{subject:string,text:string}
     */
    public static function customer(array $row, VenueSettings $venue): array
    {
        $lines = [
            "Hi {$row['first_name']},",
            '',
            "Your booking at {$venue->venueName} is confirmed.",
            '',
            'Confirmation code: ' . $row['confirmation_code'],
            'When: ' . self::when($row, $venue),
            'Stations: ' . self::stations($row),
            'Total: ' . self::money((int) $row['total_cents'], (string) $row['currency'])
                . ((int) $row['total_cents'] > 0 && $row['payment_id'] === null ? ' (payable at the venue)' : ''),
        ];
        if ($venue->venueAddress !== '') {
            $lines[] = 'Where: ' . $venue->venueAddress;
        }
        if ($venue->venuePhone !== '') {
            $lines[] = 'Questions? Call ' . $venue->venuePhone . ' and quote your confirmation code.';
        }
        $lines[] = '';
        $lines[] = 'Please arrive a few minutes early. See you soon!';
        $lines[] = $venue->venueName;

        return ['subject' => "Booking confirmed: {$venue->venueName}, " . self::dateWord($row, $venue), 'text' => implode("\n", $lines)];
    }

    /**
     * @param array<string,mixed> $row
     * @return array{subject:string,text:string}
     */
    public static function venue(array $row, VenueSettings $venue): array
    {
        $lines = [
            'New booking ' . $row['confirmation_code'],
            '',
            'When: ' . self::when($row, $venue),
            'Stations: ' . self::stations($row),
            'Guest: ' . trim($row['first_name'] . ' ' . $row['last_name']),
            'Email: ' . $row['email'],
            'Phone: ' . $row['phone'],
            'Total: ' . self::money((int) $row['total_cents'], (string) $row['currency'])
                . ($row['payment_id'] === null ? ' (unpaid)' : ' (paid, ' . $row['payment_provider'] . ')'),
        ];
        if (is_string($row['comments']) && $row['comments'] !== '') {
            $lines[] = 'Comments: ' . $row['comments'];
        }

        return ['subject' => 'New booking ' . $row['confirmation_code'] . ': ' . self::when($row, $venue), 'text' => implode("\n", $lines)];
    }

    /** @param array<string,mixed> $row */
    public static function when(array $row, VenueSettings $venue): string
    {
        $tz = $venue->tz();
        $start = (new \DateTimeImmutable((string) $row['date'] . ' 00:00:00', $tz))
            ->setTime(intdiv((int) $row['start_minute'], 60), (int) $row['start_minute'] % 60);
        $end = $start->modify('+' . (int) $row['duration_minutes'] . ' minutes');

        return $start->format('l, F j, Y \a\t g:i A') . ' to ' . $end->format('g:i A');
    }

    public static function money(int $cents, string $currency): string
    {
        return sprintf('%s %d.%02d', $currency, intdiv($cents, 100), $cents % 100);
    }

    /** @param array<string,mixed> $row */
    private static function stations(array $row): string
    {
        $numbers = is_array($row['stations']) ? $row['stations'] : [];

        return count($numbers) . ' (' . implode(', ', array_map(static fn (int $n): string => '#' . $n, $numbers)) . ')';
    }

    /** @param array<string,mixed> $row */
    private static function dateWord(array $row, VenueSettings $venue): string
    {
        return (new \DateTimeImmutable((string) $row['date'] . ' 00:00:00', $venue->tz()))->format('D M j');
    }
}
