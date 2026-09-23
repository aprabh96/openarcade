<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

use ArcadeOS\Auth\AdminUser;
use ArcadeOS\Auth\Csrf;
use ArcadeOS\Db\Connection;
use ArcadeOS\Domain\BookingRejected;
use ArcadeOS\Support\Config;
use ArcadeOS\Support\SystemClock;

final class App
{
    private const REJECTION_STATUS = [
        'validation_failed' => 422,
        'slot_unavailable' => 409,
        'hold_expired' => 409,
        'wrong_status' => 409,
        'not_found' => 404,
    ];

    private const REJECTION_MESSAGE = [
        'validation_failed' => 'Some fields are not valid.',
        'date_in_past' => 'That date has already passed.',
        'too_far_ahead' => 'That date is too far ahead to book online.',
        'closed' => 'The venue is closed on that date.',
        'outside_hours' => 'That time is outside opening hours.',
        'off_grid' => 'Start times must be on the booking grid.',
        'too_soon' => 'That start time is too soon; please pick a later one.',
        'duration_not_offered' => 'That session length is not offered on that day.',
        'invalid_station_count' => 'That number of stations is not available.',
        'slot_unavailable' => 'That time is no longer available. Please pick another.',
        'hold_expired' => 'The hold on that time expired and it was taken.',
        'wrong_status' => 'That reservation cannot be changed in its current state.',
        'not_found' => 'No such reservation.',
    ];

    private Router $router;
    private SecurityHeaders $headers;

    public function __construct(private Services $services)
    {
        $this->headers = new SecurityHeaders($services->config->embedAllowedOrigins());
        $this->router = new Router();
        $public = new PublicApi($services);
        $admin = new AdminApi($services);

        foreach (['book', 'admin'] as $page) {
            $file = $services->config->rootDir() . "/public/{$page}/index.html";
            $serve = static fn (Request $r): Response => is_file($file)
                ? Response::html((string) file_get_contents($file))
                : Response::error('not_found', 'Page not installed.', 404);
            $this->router->add('GET', "/{$page}", $serve);
            $this->router->add('GET', "/{$page}/", $serve);
        }

        $this->router->add('GET', '/api/venue', fn (Request $r): Response => $public->venue($r));
        $this->router->add('GET', '/api/availability', fn (Request $r): Response => $public->availability($r));
        $this->router->add('GET', '/api/booking-token', fn (Request $r): Response => $public->bookingToken($r));
        $this->router->add('POST', '/api/reservations', fn (Request $r): Response => $public->createReservation($r));

        $this->router->add('POST', '/api/admin/login', fn (Request $r): Response => $admin->login($r));
        $this->router->add('POST', '/api/admin/logout', fn (Request $r): Response => $admin->logout($r));
        $this->router->add('GET', '/api/admin/me', fn (Request $r): Response => $admin->me($r, $this->guard($r)));

        $this->router->add('GET', '/api/admin/reservations', fn (Request $r): Response => $admin->listReservations($r, $this->guard($r)));
        $this->router->add('POST', '/api/admin/reservations', fn (Request $r): Response => $admin->createReservation($r, $this->guard($r)));
        $this->router->add('PATCH', '/api/admin/reservations/{id}', fn (Request $r, array $p): Response => $admin->updateReservation($r, self::id($p), $this->guard($r)));
        $this->router->add('POST', '/api/admin/reservations/{id}/cancel', fn (Request $r, array $p): Response => $admin->cancelReservation($r, self::id($p), $this->guard($r)));
        $this->router->add('POST', '/api/admin/reservations/{id}/timer', fn (Request $r, array $p): Response => $admin->timer($r, self::id($p), $this->guard($r)));
        $this->router->add('POST', '/api/admin/stations/{number}/command', fn (Request $r, array $p): Response => $admin->stationCommand($r, self::id($p, 'number'), $this->guard($r)));

        foreach (['settings', 'hours', 'prices', 'closures', 'stations'] as $section) {
            $getter = 'get' . ucfirst($section);
            $putter = 'put' . ucfirst($section);
            $this->router->add('GET', "/api/admin/{$section}", fn (Request $r): Response => $admin->{$getter}($r, $this->guard($r)));
            $this->router->add('PUT', "/api/admin/{$section}", fn (Request $r): Response => $admin->{$putter}($r, $this->guard($r)));
        }
    }

    public static function fromConfig(Config $config): self
    {
        return new self(Services::build($config, Connection::fromConfig($config), new SystemClock()));
    }

    public function handle(Request $request): Response
    {
        try {
            $response = $this->router->dispatch($request);
        } catch (ApiError $error) {
            $response = $error->toResponse();
        } catch (BookingRejected $rejected) {
            $response = self::rejected($rejected);
        } catch (\InvalidArgumentException $error) {
            $response = Response::error('validation_failed', $error->getMessage(), 422);
        } catch (\Throwable $error) {
            $this->services->logger->error('unhandled exception', [
                'type' => $error::class,
                'message' => $error->getMessage(),
                'file' => basename($error->getFile()),
                'line' => $error->getLine(),
                'path' => $request->path,
            ]);
            $response = Response::error(
                'server_error',
                $this->services->config->debug() ? $error::class . ': ' . $error->getMessage() : 'Something went wrong on our side. Please try again.',
                500,
            );
        }

        return $this->headers->apply($response, $request->secure);
    }

    public static function rejected(BookingRejected $rejected): Response
    {
        $details = $rejected->reason === 'validation_failed' ? ['fields' => $rejected->fieldErrors] : [];

        return Response::error(
            $rejected->reason,
            self::REJECTION_MESSAGE[$rejected->reason] ?? 'The booking was refused.',
            self::REJECTION_STATUS[$rejected->reason] ?? 422,
            $details,
        );
    }

    /** Admin routes: a signed-in admin, and a valid CSRF token on every non-GET request. */
    private function guard(Request $request): AdminUser
    {
        $user = $this->services->auth->current($request->session);
        if ($user === null) {
            throw ApiError::unauthenticated();
        }
        if ($request->method !== 'GET' && !Csrf::verify($request->session, $request->header('X-CSRF-Token'))) {
            throw ApiError::forbidden('Missing or invalid CSRF token. Reload the page and try again.');
        }

        return $user;
    }

    /** @param array<string,string> $params */
    private static function id(array $params, string $key = 'id'): int
    {
        $value = $params[$key] ?? '';
        if (preg_match('/^[1-9]\d{0,9}$/', $value) !== 1) {
            throw ApiError::notFound();
        }

        return (int) $value;
    }
}
