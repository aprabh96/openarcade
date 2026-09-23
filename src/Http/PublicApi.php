<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

use ArcadeOS\Domain\Availability;
use ArcadeOS\Domain\BookingRejected;
use ArcadeOS\Domain\BookingRequest;
use ArcadeOS\Domain\HoursRepository;
use ArcadeOS\Support\IpHash;

/** Endpoints the booking page uses. None of them ever returns another customer's details. */
final class PublicApi
{
    private const PUBLIC_FIELDS = [
        'confirmation_code', 'status', 'date', 'start_minute', 'end_minute', 'duration_minutes',
        'station_count', 'stations', 'subtotal_cents', 'tax_cents', 'total_cents', 'currency', 'first_name',
    ];

    public function __construct(private Services $s)
    {
    }

    public function venue(Request $request): Response
    {
        $venue = $this->s->settings->load();
        $nowLocal = $this->s->clock->now()->setTimezone($venue->tz());
        $prices = $this->s->prices->all();
        $durations = array_values(array_unique(array_column($prices, 'duration_minutes')));
        sort($durations);

        $payment = ['mode' => $this->s->gateway->mode()];
        if ($payment['mode'] === 'square') {
            $square = $this->s->config->square();
            $payment['square'] = [
                'application_id' => $square['applicationId'],
                'location_id' => $square['locationId'],
                'environment' => $square['environment'],
            ];
        }

        return Response::json([
            'venue' => [
                'name' => $venue->venueName,
                'timezone' => $venue->timezone,
                'currency' => $venue->currency,
                'tax_rate_bp' => $venue->taxRateBp,
                'slot_step_minutes' => $venue->slotStepMinutes,
                'buffer_minutes' => $venue->bufferMinutes,
                'min_lead_minutes' => $venue->minLeadMinutes,
                'max_advance_days' => $venue->maxAdvanceDays,
                'station_count' => count($this->s->stations->activeNumbersById()),
                'brand_color' => $venue->brandColor,
                'logo_url' => $venue->logoUrl,
                'address' => $venue->venueAddress,
                'phone' => $venue->venuePhone,
            ],
            'hours' => array_values($this->s->hours->weekdays()),
            'durations' => $durations,
            'prices' => $prices,
            'payment' => $payment,
            'today' => $nowLocal->format('Y-m-d'),
            'now_minute' => (int) $nowLocal->format('G') * 60 + (int) $nowLocal->format('i'),
        ]);
    }

    public function availability(Request $request): Response
    {
        $this->rateLimit($request, 'availability', 300, 300);

        $date = $request->query('date') ?? '';
        try {
            HoursRepository::assertDate($date);
        } catch (\InvalidArgumentException) {
            throw ApiError::validation(['date' => 'Date must be YYYY-MM-DD.']);
        }
        $duration = self::queryInt($request, 'duration', 5, 1440);
        $stations = self::queryInt($request, 'stations', 1, 200, 1);

        $venue = $this->s->settings->load();
        $tz = $venue->tz();
        $now = $this->s->clock->now();
        $nowLocal = $now->setTimezone($tz);
        $today = $nowLocal->format('Y-m-d');
        if ($date < $today) {
            throw new BookingRejected('date_in_past');
        }
        if ($date > $nowLocal->modify("+{$venue->maxAdvanceDays} days")->format('Y-m-d')) {
            throw new BookingRejected('too_far_ahead');
        }
        $weekday = (int) (new \DateTimeImmutable($date))->format('w');
        if ($this->s->prices->load()->priceCents($weekday, $duration) === null) {
            throw new BookingRejected('duration_not_offered');
        }

        $hours = $this->s->hours->forDate($date);
        $base = [
            'date' => $date,
            'duration_minutes' => $duration,
            'stations' => $stations,
            'slot_step_minutes' => $venue->slotStepMinutes,
        ];
        if ($hours === null) {
            return Response::json($base + ['closed' => true, 'open_minute' => null, 'close_minute' => null, 'slots' => []]);
        }

        $earliest = 0;
        if ($date === $today) {
            $earliest = (int) $nowLocal->format('G') * 60 + (int) $nowLocal->format('i') + $venue->minLeadMinutes;
        }
        $numbersById = $this->s->stations->activeNumbersById();
        $blocks = $this->s->reservationRepository->blocksForDate($date, $now, $tz);
        $slots = [];
        foreach (Availability::slots($hours, array_keys($numbersById), $blocks, $duration, $venue->slotStepMinutes, $venue->bufferMinutes, $earliest) as $slot) {
            $slots[] = ['start_minute' => $slot->startMinute, 'free' => count($slot->freeStationIds)];
        }

        return Response::json($base + [
            'closed' => false,
            'open_minute' => $hours->openMinute,
            'close_minute' => $hours->closeMinute,
            'slots' => $slots,
        ]);
    }

    public function bookingToken(Request $request): Response
    {
        $this->rateLimit($request, 'token', 60, 600);

        return Response::json(['token' => $this->s->bookingToken->issue()]);
    }

    public function createReservation(Request $request): Response
    {
        $this->rateLimit($request, 'book', 10, 600);
        $this->assertSameOrigin($request);
        if ($request->malformedBody) {
            throw ApiError::badRequest('The request body is not valid JSON.');
        }
        if (!$this->s->bookingToken->verify($request->string('booking_token'))) {
            throw new ApiError('booking_expired', 'Your booking session has expired. Please reload the page and try again.', 403);
        }

        $booking = new BookingRequest(
            $request->string('date') ?? '',
            $request->int('start_minute') ?? -1,
            $request->int('duration_minutes') ?? 0,
            $request->int('station_count') ?? 0,
            $request->string('first_name') ?? '',
            $request->string('last_name') ?? '',
            $request->string('email') ?? '',
            $request->string('phone') ?? '',
            $request->string('comments'),
        );
        $requestId = $request->string('request_id');
        if ($requestId !== null && preg_match('/^[A-Za-z0-9-]{16,64}$/', $requestId) !== 1) {
            throw ApiError::validation(['request_id' => 'Must be 16 to 64 letters, digits or dashes.']);
        }
        $row = $this->s->flow->book($booking, $request->string('payment_token'), $requestId);

        return Response::json(['reservation' => self::publicView($row)], 201);
    }

    /**
     * @param array<string,mixed> $row
     * @return array<string,mixed>
     */
    public static function publicView(array $row): array
    {
        return array_intersect_key($row, array_flip(self::PUBLIC_FIELDS));
    }

    private function rateLimit(Request $request, string $bucket, int $limit, int $windowSeconds): void
    {
        $ipHash = IpHash::of($request->ip, $this->s->config->appKey());
        if (!$this->s->limiter->allow($bucket, $ipHash, $limit, $windowSeconds, $this->s->clock->now())) {
            throw ApiError::rateLimited();
        }
    }

    /** Browsers always send Origin on cross-site POSTs; a booking must come from our own pages. */
    private function assertSameOrigin(Request $request): void
    {
        $origin = $request->header('Origin');
        if ($origin === null) {
            $referer = $request->header('Referer');
            $origin = $referer === null ? null : self::originOf($referer);
        }
        if ($origin === null) {
            throw new ApiError('cross_site', 'Cross-site request refused. Reload the booking page and try again.', 403);
        }
        $origin = strtolower(rtrim($origin, '/'));
        $allowed = [strtolower(self::originOf($this->s->config->appUrl()) ?? '')];
        if (!in_array($origin, $allowed, true)) {
            throw new ApiError('cross_site', 'Cross-site request refused. Check APP_URL matches the address in the browser.', 403);
        }
    }

    private static function originOf(string $url): ?string
    {
        $parts = parse_url($url);
        if (!is_array($parts) || !isset($parts['scheme'], $parts['host'])) {
            return null;
        }

        return $parts['scheme'] . '://' . $parts['host'] . (isset($parts['port']) ? ':' . $parts['port'] : '');
    }

    private static function queryInt(Request $request, string $key, int $min, int $max, ?int $default = null): int
    {
        $raw = $request->query($key);
        if ($raw === null || $raw === '') {
            if ($default !== null) {
                return $default;
            }
            throw ApiError::validation([$key => 'Required.']);
        }
        if (preg_match('/^\d{1,6}$/', $raw) !== 1 || (int) $raw < $min || (int) $raw > $max) {
            throw ApiError::validation([$key => "Must be a whole number from {$min} to {$max}."]);
        }

        return (int) $raw;
    }
}
