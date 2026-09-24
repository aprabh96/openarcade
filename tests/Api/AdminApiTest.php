<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Api;

use OpenArcade\Domain\BookingRules;
use OpenArcade\Tests\Integration\VenueFixture;

final class AdminApiTest extends ApiTestCase
{
    public function testLoginRejectsWrongPasswordAndThrottlesAfterFiveFailures(): void
    {
        $this->createAdmin();
        for ($i = 1; $i <= 5; $i++) {
            $response = $this->request('POST', '/api/admin/login', ['username' => 'owner', 'password' => 'wrong-password-' . $i]);
            self::assertSame(401, $response->status, "attempt {$i}");
        }
        $blocked = $this->request('POST', '/api/admin/login', ['username' => 'owner', 'password' => self::ADMIN_PASSWORD]);
        self::assertSame(429, $blocked->status, 'even the right password is refused while blocked');
        self::assertSame('too_many_attempts', $this->json($blocked)['error']['code']);

        $this->clock->advanceMinutes(16);
        self::assertSame(200, $this->request('POST', '/api/admin/login', ['username' => 'owner', 'password' => self::ADMIN_PASSWORD])->status);
    }

    public function testLoginStartsAFreshSessionAndReturnsACsrfToken(): void
    {
        $csrf = $this->signIn();
        self::assertSame(64, strlen($csrf));
        self::assertSame(1, $this->session->regenerations);
        $me = $this->json($this->request('GET', '/api/admin/me'));
        self::assertSame('owner', $me['user']['username']);
        self::assertSame($csrf, $me['csrf_token']);
    }

    public function testAdminRoutesRequireASessionAndWritesRequireCsrf(): void
    {
        self::assertSame(401, $this->request('GET', '/api/admin/reservations?date=2026-09-22')->status);
        $csrf = $this->signIn();
        self::assertSame(200, $this->request('GET', '/api/admin/reservations?date=2026-09-22')->status);
        $body = ['date' => '2026-09-22', 'start_minute' => 600, 'duration_minutes' => 60, 'station_count' => 1, 'first_name' => 'Walk', 'last_name' => 'In'];
        self::assertSame(403, $this->request('POST', '/api/admin/reservations', $body)->status);
        self::assertSame(403, $this->request('POST', '/api/admin/reservations', $body, ['X-CSRF-Token' => str_repeat('a', 64)])->status);
        self::assertSame(201, $this->adminRequest($csrf, 'POST', '/api/admin/reservations', $body)->status);
    }

    public function testListIncludesContactDetailsStationsAndHours(): void
    {
        $this->services->reservations->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        $this->signIn();
        $data = $this->json($this->request('GET', '/api/admin/reservations?date=2026-09-22'));
        self::assertSame(['open_minute' => 600, 'close_minute' => 1320], $data['hours']);
        self::assertCount(2, $data['stations']);
        self::assertCount(1, $data['reservations']);
        $row = $data['reservations'][0];
        self::assertSame('alex.rivera@example.com', $row['email']);
        self::assertSame([1, 2], $row['stations']);
        self::assertSame(600, $row['start_minute']);
        self::assertSame(660, $row['end_minute']);
        self::assertSame('confirmed', $row['status']);
    }

    public function testWalkInCanBeComplimentaryAndOffGrid(): void
    {
        $csrf = $this->signIn();
        $response = $this->adminRequest($csrf, 'POST', '/api/admin/reservations', [
            'date' => '2026-09-21', 'start_minute' => 617, 'duration_minutes' => 60, 'station_count' => 1,
            'first_name' => 'Walk', 'last_name' => 'In', 'complimentary' => true, 'comments' => 'birthday',
        ]);
        self::assertSame(201, $response->status, $response->body);
        $row = $this->json($response)['reservation'];
        self::assertSame(0, $row['total_cents']);
        self::assertSame('admin', $row['created_by']);
        self::assertSame('birthday', $row['comments']);
    }

    public function testRescheduleChecksAvailabilityAndContactEditsDoNot(): void
    {
        $this->services->reservations->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        $mine = $this->services->reservations->create(VenueFixture::request('2026-09-22', 720), BookingRules::customer(false));
        $csrf = $this->signIn();

        $conflict = $this->adminRequest($csrf, 'PATCH', "/api/admin/reservations/{$mine->id}", ['start_minute' => 600]);
        self::assertSame(409, $conflict->status, $conflict->body);
        $buffer = $this->adminRequest($csrf, 'PATCH', "/api/admin/reservations/{$mine->id}", ['start_minute' => 660]);
        self::assertSame(409, $buffer->status, 'inside the 10 minute buffer');
        $moved = $this->adminRequest($csrf, 'PATCH', "/api/admin/reservations/{$mine->id}", ['start_minute' => 690, 'duration_minutes' => 90]);
        self::assertSame(200, $moved->status, $moved->body);
        $row = $this->json($moved)['reservation'];
        self::assertSame(690, $row['start_minute']);
        self::assertSame(3550 + 332, $row['total_cents'], 'amounts recomputed when the duration changes');

        $renamed = $this->adminRequest($csrf, 'PATCH', "/api/admin/reservations/{$mine->id}", ['first_name' => 'Alexandra', 'comments' => 'VIP']);
        self::assertSame(200, $renamed->status);
        $row = $this->json($renamed)['reservation'];
        self::assertSame('Alexandra', $row['first_name']);
        self::assertSame('VIP', $row['comments']);
        self::assertSame(690, $row['start_minute']);

        self::assertSame(404, $this->adminRequest($csrf, 'PATCH', '/api/admin/reservations/999', ['first_name' => 'x'])->status);
        self::assertSame(404, $this->adminRequest($csrf, 'PATCH', '/api/admin/reservations/abc', ['first_name' => 'x'])->status);
    }

    public function testCancelFreesTheStationAndCannotRepeat(): void
    {
        $mine = $this->services->reservations->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        $csrf = $this->signIn();
        $cancelled = $this->adminRequest($csrf, 'POST', "/api/admin/reservations/{$mine->id}/cancel");
        self::assertSame(200, $cancelled->status);
        self::assertSame('cancelled', $this->json($cancelled)['reservation']['status']);
        self::assertSame(409, $this->adminRequest($csrf, 'POST', "/api/admin/reservations/{$mine->id}/cancel")->status);

        $free = $this->json($this->request('GET', '/api/availability?date=2026-09-22&duration=60'));
        self::assertSame(2, array_column($free['slots'], 'free', 'start_minute')[600]);
    }

    public function testConfigurationRoundTrips(): void
    {
        $csrf = $this->signIn();

        $settings = $this->adminRequest($csrf, 'PUT', '/api/admin/settings', ['settings' => ['venue_name' => 'Orbit VR', 'buffer_minutes' => 15, 'brand_color' => '#123ABC']]);
        self::assertSame(200, $settings->status, $settings->body);
        self::assertSame('Orbit VR', $this->json($settings)['settings']['venue_name']);
        self::assertSame('#123abc', $this->json($settings)['settings']['brand_color']);
        $bad = $this->adminRequest($csrf, 'PUT', '/api/admin/settings', ['settings' => ['timezone' => 'Mars/Olympus', 'nonsense' => 1]]);
        self::assertSame(422, $bad->status);
        self::assertSame(['timezone', 'nonsense'], array_keys($this->json($bad)['error']['details']['fields']));

        $hours = $this->adminRequest($csrf, 'PUT', '/api/admin/hours', ['weekdays' => [['weekday' => 1, 'open_minute' => 720, 'close_minute' => 1380, 'closed' => false], ['weekday' => 2, 'open_minute' => 600, 'close_minute' => 1320, 'closed' => true]]]);
        self::assertSame(200, $hours->status, $hours->body);
        $weekdays = $this->json($hours)['weekdays'];
        self::assertSame(720, $weekdays[1]['open_minute']);
        self::assertTrue($weekdays[2]['closed']);
        self::assertSame(422, $this->adminRequest($csrf, 'PUT', '/api/admin/hours', ['weekdays' => [['weekday' => 1, 'open_minute' => 900, 'close_minute' => 600]]])->status);

        $prices = $this->adminRequest($csrf, 'PUT', '/api/admin/prices', ['set' => [['weekday' => -1, 'duration_minutes' => 120, 'price_cents' => 4500]], 'remove' => [['weekday' => 6, 'duration_minutes' => 60]]]);
        self::assertSame(200, $prices->status, $prices->body);
        $rows = $this->json($prices)['prices'];
        self::assertContains(['weekday' => -1, 'duration_minutes' => 120, 'price_cents' => 4500], $rows);
        self::assertNotContains(['weekday' => 6, 'duration_minutes' => 60, 'price_cents' => 3000], $rows);

        $closures = $this->adminRequest($csrf, 'PUT', '/api/admin/closures', [
            'add_closed' => [['date' => '2026-12-25', 'reason' => 'Christmas']],
            'add_special' => [['date' => '2026-12-24', 'open_minute' => 600, 'close_minute' => 960]],
        ]);
        self::assertSame(200, $closures->status, $closures->body);
        self::assertSame([['date' => '2026-12-25', 'reason' => 'Christmas']], $this->json($closures)['closed_dates']);
        self::assertSame(960, $this->json($closures)['special_hours'][0]['close_minute']);
        $removed = $this->json($this->adminRequest($csrf, 'PUT', '/api/admin/closures', ['remove_closed' => ['2026-12-25'], 'remove_special' => ['2026-12-24']]));
        self::assertSame([], $removed['closed_dates']);
        self::assertSame([], $removed['special_hours']);

        $stations = $this->adminRequest($csrf, 'PUT', '/api/admin/stations', ['count' => 3, 'labels' => ['3' => 'Racing sim']]);
        self::assertSame(200, $stations->status, $stations->body);
        self::assertSame([['number' => 1, 'label' => 'Station 1'], ['number' => 2, 'label' => 'Station 2'], ['number' => 3, 'label' => 'Racing sim']], $this->json($stations)['stations']);
        self::assertSame(422, $this->adminRequest($csrf, 'PUT', '/api/admin/stations', ['labels' => ['9' => 'Ghost']])->status);
        $fewer = $this->adminRequest($csrf, 'PUT', '/api/admin/stations', ['count' => 2, 'labels' => ['1' => 'Station 1', '2' => 'Station 2', '3' => 'Racing sim']]);
        self::assertSame(200, $fewer->status, 'labels for stations removed in the same save are ignored: ' . $fewer->body);
        self::assertCount(2, $this->json($fewer)['stations']);
        self::assertSame(422, $this->adminRequest($csrf, 'PUT', '/api/admin/stations', ['count' => 0])->status);
        self::assertSame(422, $this->adminRequest($csrf, 'PUT', '/api/admin/hours', ['weekdays' => [['weekday' => 1, 'open_minute' => 600, 'close_minute' => 1320, 'closed' => 'false']]])->status, 'closed must be a real boolean');
    }

    public function testLogoutAndIdleTimeoutEndTheSession(): void
    {
        $csrf = $this->signIn();
        self::assertSame(204, $this->adminRequest($csrf, 'POST', '/api/admin/logout')->status);
        self::assertSame(401, $this->request('GET', '/api/admin/me')->status);

        $this->signIn();
        $this->clock->advanceMinutes(481);
        self::assertSame(401, $this->request('GET', '/api/admin/me')->status);
    }
}
