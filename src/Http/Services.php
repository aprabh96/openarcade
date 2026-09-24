<?php

declare(strict_types=1);

namespace OpenArcade\Http;

use OpenArcade\Auth\AdminAuth;
use OpenArcade\Auth\BookingToken;
use OpenArcade\Auth\LoginThrottle;
use OpenArcade\Domain\BookingFlow;
use OpenArcade\Domain\HoursRepository;
use OpenArcade\Domain\PriceRepository;
use OpenArcade\Domain\ReservationRepository;
use OpenArcade\Domain\Reservations;
use OpenArcade\Domain\StationRepository;
use OpenArcade\Mail\Mailer;
use OpenArcade\Payments\PaymentGateway;
use OpenArcade\Settings\SettingsRepository;
use OpenArcade\Support\Clock;
use OpenArcade\Support\Config;
use OpenArcade\Support\Logger;
use PDO;

/** Everything the HTTP layer needs, built once per request. */
final class Services
{
    public function __construct(
        public readonly Config $config,
        public readonly PDO $pdo,
        public readonly Clock $clock,
        public readonly Logger $logger,
        public readonly SettingsRepository $settings,
        public readonly HoursRepository $hours,
        public readonly PriceRepository $prices,
        public readonly StationRepository $stations,
        public readonly ReservationRepository $reservationRepository,
        public readonly Reservations $reservations,
        public readonly PaymentGateway $gateway,
        public readonly Mailer $mailer,
        public readonly BookingFlow $flow,
        public readonly BookingToken $bookingToken,
        public readonly AdminAuth $auth,
        public readonly LoginThrottle $throttle,
        public readonly RateLimiter $limiter,
    ) {
    }

    public static function build(
        Config $config,
        PDO $pdo,
        Clock $clock,
        ?PaymentGateway $gateway = null,
        ?Mailer $mailer = null,
        ?Logger $logger = null,
    ): self {
        $logger ??= new Logger($config->rootDir() . '/storage/logs/app.log');
        $gateway ??= Drivers::gateway($config, $logger);
        $mailer ??= Drivers::mailer($config, $logger);

        $settings = new SettingsRepository($pdo);
        $hours = new HoursRepository($pdo);
        $prices = new PriceRepository($pdo);
        $stations = new StationRepository($pdo);
        $repository = new ReservationRepository($pdo);
        $reservations = new Reservations($pdo, $repository, $hours, $prices, $stations, $settings, $clock);

        return new self(
            $config,
            $pdo,
            $clock,
            $logger,
            $settings,
            $hours,
            $prices,
            $stations,
            $repository,
            $reservations,
            $gateway,
            $mailer,
            new BookingFlow($reservations, $repository, $settings, $gateway, $mailer, $logger),
            new BookingToken($config->appKey(), $clock),
            new AdminAuth($pdo, $clock, $config->sessionIdleMinutes()),
            new LoginThrottle($pdo),
            new RateLimiter($pdo),
        );
    }
}
