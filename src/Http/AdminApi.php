<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

use ArcadeOS\Auth\AdminUser;
use ArcadeOS\Auth\Csrf;
use ArcadeOS\Domain\BookingRejected;
use ArcadeOS\Domain\BookingRequest;
use ArcadeOS\Domain\BookingRules;
use ArcadeOS\Domain\HoursRepository;
use ArcadeOS\Realtime\Notifier;
use ArcadeOS\Settings\VenueSettings;
use ArcadeOS\Support\IpHash;

/** Endpoints the staff dashboard uses. Every method except login/logout runs behind App::guard(). */
final class AdminApi
{
    public function __construct(private Services $s)
    {
    }

    // ----------------------------------------------------------------- session

    public function login(Request $request): Response
    {
        $now = $this->s->clock->now();
        $ipHash = IpHash::of($request->ip, $this->s->config->appKey());
        if (!$this->s->limiter->allow('login', $ipHash, 20, 900, $now)) {
            throw ApiError::rateLimited();
        }
        $username = $request->string('username') ?? '';
        $password = is_string($request->body['password'] ?? null) ? $request->body['password'] : '';
        if ($username === '' || $password === '') {
            throw ApiError::validation(['username' => 'Username and password are required.']);
        }
        if ($this->s->throttle->isBlocked($username, $ipHash, $now)) {
            throw new ApiError('too_many_attempts', 'Too many failed sign-ins. Please wait 15 minutes.', 429);
        }
        $user = $this->s->auth->login($request->session, $username, $password);
        $this->s->throttle->record($username, $ipHash, $user !== null, $now);
        if ($user === null) {
            throw new ApiError('invalid_credentials', 'Wrong username or password.', 401);
        }

        return Response::json(self::sessionView($request, $user));
    }

    public function logout(Request $request): Response
    {
        $this->s->auth->logout($request->session);

        return Response::empty();
    }

    public function me(Request $request, AdminUser $user): Response
    {
        return Response::json(self::sessionView($request, $user));
    }

    // ----------------------------------------------------------------- reservations

    public function listReservations(Request $request, AdminUser $user): Response
    {
        $date = $request->query('date') ?? '';
        try {
            HoursRepository::assertDate($date);
        } catch (\InvalidArgumentException) {
            throw ApiError::validation(['date' => 'Date must be YYYY-MM-DD.']);
        }
        $venue = $this->s->settings->load();

        return Response::json([
            'date' => $date,
            'hours' => self::hoursView($this->s->hours->forDate($date)),
            'stations' => $this->s->stations->active(),
            'reservations' => $this->s->reservationRepository->listForDate($date, $venue->tz()),
        ]);
    }

    public function createReservation(Request $request, AdminUser $user): Response
    {
        self::assertJson($request);
        $booking = self::bookingFromBody($request, null);
        $reservation = $this->s->reservations->create($booking, BookingRules::admin($request->bool('complimentary') ?? false));

        return Response::json(['reservation' => $this->row($reservation->id)], 201);
    }

    public function updateReservation(Request $request, int $id, AdminUser $user): Response
    {
        self::assertJson($request);
        $existing = $this->row($id);
        $booking = self::bookingFromBody($request, $existing);
        $reservation = $this->s->reservations->update($id, $booking, BookingRules::admin($request->bool('complimentary') ?? false));

        return Response::json(['reservation' => $this->row($reservation->id)]);
    }

    public function cancelReservation(Request $request, int $id, AdminUser $user): Response
    {
        $this->row($id);
        $this->s->reservations->cancel($id);

        return Response::json(['reservation' => $this->row($id)]);
    }

    public function timer(Request $request, int $id, AdminUser $user): Response
    {
        self::assertJson($request);
        $action = $request->string('action') ?? '';
        $minutes = $request->int('minutes');
        $timer = $this->s->reservations->setTimer($id, $action, $minutes);
        $row = $this->row($id);

        $command = match ($action) {
            'start' => 'START_SESSION',
            'extend' => 'ADD_TIME',
            default => 'STOP_SESSION',
        };
        $delivery = $this->deliver($row['stations'], $command, $timer['minutes']);

        return Response::json(['timer' => $timer, 'delivery' => $delivery, 'reservation' => $row]);
    }

    public function stationCommand(Request $request, int $number, AdminUser $user): Response
    {
        self::assertJson($request);
        $command = strtoupper($request->string('command') ?? '');
        if (!in_array($command, Notifier::COMMANDS, true)) {
            throw ApiError::validation(['command' => 'Must be one of ' . implode(', ', Notifier::COMMANDS) . '.']);
        }
        $value = $request->int('value');
        if ($value === null || $value < 0 || $value > 1440) {
            throw ApiError::validation(['value' => 'Must be a whole number of minutes from 0 to 1440.']);
        }
        if (!in_array($number, $this->s->stations->activeNumbersById(), true)) {
            throw ApiError::notFound('No such station.');
        }
        $delivery = $this->deliver([$number], $command, $value);
        if ($delivery === 'failed') {
            throw new ApiError('delivery_failed', 'The station could not be reached. Check the real-time settings.', 502);
        }

        return Response::json(['station' => $number, 'command' => $command, 'value' => $value, 'delivery' => $delivery]);
    }

    // ----------------------------------------------------------------- configuration

    public function getSettings(Request $request, AdminUser $user): Response
    {
        return Response::json(['settings' => $this->s->settings->load()->toArray(), 'timezones' => \DateTimeZone::listIdentifiers()]);
    }

    public function putSettings(Request $request, AdminUser $user): Response
    {
        self::assertJson($request);
        $values = $request->body['settings'] ?? $request->body;
        if (!is_array($values)) {
            throw ApiError::validation(['settings' => 'Must be an object of setting values.']);
        }
        $errors = [];
        foreach ($values as $key => $value) {
            if (!is_string($key) || !array_key_exists($key, VenueSettings::DEFAULTS)) {
                $errors[(string) $key] = 'Unknown setting.';
                continue;
            }
            if (!is_scalar($value) && $value !== null) {
                $errors[$key] = 'Must be a single value.';
                continue;
            }
            try {
                $this->s->settings->set($key, is_bool($value) ? ($value ? '1' : '0') : trim((string) $value));
            } catch (\InvalidArgumentException $error) {
                $errors[$key] = $error->getMessage();
            }
        }
        if ($errors !== []) {
            throw ApiError::validation($errors);
        }

        return $this->getSettings($request, $user);
    }

    public function getHours(Request $request, AdminUser $user): Response
    {
        return Response::json(['weekdays' => array_values($this->s->hours->weekdays())]);
    }

    public function putHours(Request $request, AdminUser $user): Response
    {
        self::assertJson($request);
        foreach (self::listOf($request, 'weekdays') as $index => $item) {
            $weekday = self::intField($item, 'weekday', "weekdays.{$index}", 0, 6);
            $open = self::intField($item, 'open_minute', "weekdays.{$index}", 0, 1439);
            $close = self::intField($item, 'close_minute', "weekdays.{$index}", 1, 1440);
            $closed = isset($item['closed']) ? (bool) $item['closed'] : false;
            $this->s->hours->setWeekday($weekday, $open, $close, $closed);
        }

        return $this->getHours($request, $user);
    }

    public function getPrices(Request $request, AdminUser $user): Response
    {
        return Response::json(['prices' => $this->s->prices->all()]);
    }

    public function putPrices(Request $request, AdminUser $user): Response
    {
        self::assertJson($request);
        foreach (self::listOf($request, 'set', false) as $index => $item) {
            $this->s->prices->set(
                self::intField($item, 'weekday', "set.{$index}", -1, 6),
                self::intField($item, 'duration_minutes', "set.{$index}", 5, 1440),
                self::intField($item, 'price_cents', "set.{$index}", 0, 100_000_000),
            );
        }
        foreach (self::listOf($request, 'remove', false) as $index => $item) {
            $this->s->prices->remove(
                self::intField($item, 'weekday', "remove.{$index}", -1, 6),
                self::intField($item, 'duration_minutes', "remove.{$index}", 5, 1440),
            );
        }

        return $this->getPrices($request, $user);
    }

    public function getClosures(Request $request, AdminUser $user): Response
    {
        $today = $this->s->clock->now()->setTimezone($this->s->settings->load()->tz())->format('Y-m-d');

        return Response::json([
            'closed_dates' => $this->s->hours->closedDatesFrom($today),
            'special_hours' => $this->s->hours->specialHoursFrom($today),
        ]);
    }

    public function putClosures(Request $request, AdminUser $user): Response
    {
        self::assertJson($request);
        foreach (self::listOf($request, 'add_closed', false) as $index => $item) {
            $this->s->hours->addClosedDate(self::dateField($item, 'date', "add_closed.{$index}"), self::stringField($item, 'reason', 120));
        }
        foreach (self::dateList($request, 'remove_closed') as $date) {
            $this->s->hours->removeClosedDate($date);
        }
        foreach (self::listOf($request, 'add_special', false) as $index => $item) {
            $this->s->hours->setSpecialHours(
                self::dateField($item, 'date', "add_special.{$index}"),
                self::intField($item, 'open_minute', "add_special.{$index}", 0, 1439),
                self::intField($item, 'close_minute', "add_special.{$index}", 1, 1440),
            );
        }
        foreach (self::dateList($request, 'remove_special') as $date) {
            $this->s->hours->removeSpecialHours($date);
        }

        return $this->getClosures($request, $user);
    }

    public function getStations(Request $request, AdminUser $user): Response
    {
        return Response::json(['stations' => $this->s->stations->active()]);
    }

    public function putStations(Request $request, AdminUser $user): Response
    {
        self::assertJson($request);
        $count = $request->int('count');
        if ($count !== null) {
            $this->s->stations->syncCount($count);
        }
        $labels = $request->body['labels'] ?? [];
        if (!is_array($labels)) {
            throw ApiError::validation(['labels' => 'Must be an object of station number to label.']);
        }
        $active = $this->s->stations->activeNumbersById();
        foreach ($labels as $number => $label) {
            if (preg_match('/^\d{1,3}$/', (string) $number) !== 1 || !in_array((int) $number, $active, true)) {
                throw ApiError::validation(['labels' => "Station {$number} is not active."]);
            }
            if (!is_string($label)) {
                throw ApiError::validation(['labels' => "Label for station {$number} must be text."]);
            }
            $this->s->stations->setLabel((int) $number, $label);
        }

        return $this->getStations($request, $user);
    }

    // ----------------------------------------------------------------- helpers

    /** @return array<string,mixed> */
    private static function sessionView(Request $request, AdminUser $user): array
    {
        return ['user' => ['username' => $user->username], 'csrf_token' => Csrf::token($request->session)];
    }

    /** @return array<string,mixed> */
    private function row(int $id): array
    {
        $row = $this->s->reservationRepository->findRow($id, $this->s->settings->load()->tz());
        if ($row === null) {
            throw new BookingRejected('not_found');
        }

        return $row;
    }

    /** @return array{open_minute:int,close_minute:int}|null */
    private static function hoursView(?\ArcadeOS\Domain\DayHours $hours): ?array
    {
        return $hours === null ? null : ['open_minute' => $hours->openMinute, 'close_minute' => $hours->closeMinute];
    }

    /**
     * Sends a command to each station number; delivery problems are reported, never thrown, so a
     * timer change is saved even when the real-time service is down.
     *
     * @param int[] $stationNumbers
     */
    private function deliver(array $stationNumbers, string $command, int|float $value): string
    {
        if (!$this->s->notifier->enabled() && !($this->s->notifier instanceof \ArcadeOS\Realtime\NullNotifier)) {
            return 'disabled';
        }
        try {
            foreach ($stationNumbers as $number) {
                $this->s->notifier->stationCommand($number, $command, $value);
            }
        } catch (\RuntimeException $error) {
            $this->s->logger->warning('station command delivery failed', ['command' => $command, 'error' => $error->getMessage()]);

            return 'failed';
        }

        return $this->s->notifier->enabled() ? 'sent' : 'disabled';
    }

    /**
     * Builds a booking request from the body, falling back to $existing for fields that are absent.
     *
     * @param array<string,mixed>|null $existing
     */
    private static function bookingFromBody(Request $request, ?array $existing): BookingRequest
    {
        $get = static fn (string $key, mixed $fallback): mixed => array_key_exists($key, $request->body) ? null : $fallback;

        return new BookingRequest(
            $request->string('date') ?? (string) ($existing['date'] ?? ''),
            $request->int('start_minute') ?? (int) ($existing['start_minute'] ?? -1),
            $request->int('duration_minutes') ?? (int) ($existing['duration_minutes'] ?? 0),
            $request->int('station_count') ?? (int) ($existing['station_count'] ?? 0),
            $request->string('first_name') ?? (string) ($existing['first_name'] ?? ''),
            $request->string('last_name') ?? (string) ($existing['last_name'] ?? ''),
            $request->string('email') ?? (string) ($existing['email'] ?? ''),
            $request->string('phone') ?? (string) ($existing['phone'] ?? ''),
            array_key_exists('comments', $request->body) ? $request->string('comments') : ($existing['comments'] ?? $get('comments', null)),
        );
    }

    private static function assertJson(Request $request): void
    {
        if ($request->malformedBody) {
            throw ApiError::badRequest('The request body is not valid JSON.');
        }
    }

    /** @return array<int, array<string,mixed>> */
    private static function listOf(Request $request, string $key, bool $required = true): array
    {
        $value = $request->body[$key] ?? null;
        if ($value === null) {
            if ($required) {
                throw ApiError::validation([$key => 'Required.']);
            }

            return [];
        }
        if (!is_array($value) || array_keys($value) !== range(0, count($value) - 1) && $value !== []) {
            throw ApiError::validation([$key => 'Must be a list.']);
        }
        foreach ($value as $index => $item) {
            if (!is_array($item)) {
                throw ApiError::validation(["{$key}.{$index}" => 'Must be an object.']);
            }
        }

        return $value;
    }

    /** @return string[] */
    private static function dateList(Request $request, string $key): array
    {
        $value = $request->body[$key] ?? [];
        if (!is_array($value)) {
            throw ApiError::validation([$key => 'Must be a list of dates.']);
        }
        $dates = [];
        foreach ($value as $index => $date) {
            if (!is_string($date)) {
                throw ApiError::validation(["{$key}.{$index}" => 'Date must be YYYY-MM-DD.']);
            }
            try {
                HoursRepository::assertDate($date);
            } catch (\InvalidArgumentException) {
                throw ApiError::validation(["{$key}.{$index}" => 'Date must be YYYY-MM-DD.']);
            }
            $dates[] = $date;
        }

        return $dates;
    }

    /** @param array<string,mixed> $item */
    private static function intField(array $item, string $key, string $path, int $min, int $max): int
    {
        $value = $item[$key] ?? null;
        if (is_string($value) && preg_match('/^-?\d{1,9}$/', trim($value)) === 1) {
            $value = (int) trim($value);
        }
        if (!is_int($value) || $value < $min || $value > $max) {
            throw ApiError::validation(["{$path}.{$key}" => "Must be a whole number from {$min} to {$max}."]);
        }

        return $value;
    }

    /** @param array<string,mixed> $item */
    private static function dateField(array $item, string $key, string $path): string
    {
        $value = $item[$key] ?? null;
        if (!is_string($value)) {
            throw ApiError::validation(["{$path}.{$key}" => 'Date must be YYYY-MM-DD.']);
        }
        try {
            HoursRepository::assertDate($value);
        } catch (\InvalidArgumentException) {
            throw ApiError::validation(["{$path}.{$key}" => 'Date must be YYYY-MM-DD.']);
        }

        return $value;
    }

    /** @param array<string,mixed> $item */
    private static function stringField(array $item, string $key, int $maxLength): string
    {
        $value = $item[$key] ?? '';
        if (!is_string($value)) {
            return '';
        }

        return mb_substr(trim($value), 0, $maxLength);
    }
}
