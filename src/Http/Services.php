<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

use ArcadeOS\Auth\AdminAuth;
use ArcadeOS\Auth\BookingToken;
use ArcadeOS\Auth\LoginThrottle;
use ArcadeOS\Domain\BookingFlow;
use ArcadeOS\Domain\HoursRepository;
use ArcadeOS\Domain\PriceRepository;
use ArcadeOS\Domain\ReservationRepository;
use ArcadeOS\Domain\Reservations;
use ArcadeOS\Domain\StationRepository;
use ArcadeOS\Mail\Mailer;
use ArcadeOS\Payments\PaymentGateway;
use ArcadeOS\Realtime\Notifier;
use ArcadeOS\Settings\SettingsRepository;
use ArcadeOS\Support\Clock;
use ArcadeOS\Support\Config;
use ArcadeOS\Support\Logger;
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
        public readonly Notifier $notifier,
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
        ?Notifier $notifier = null,
        ?Mailer $mailer = null,
        ?Logger $logger = null,
    ): self {
        $logger ??= new Logger($config->rootDir() . '/storage/logs/app.log');
        $gateway ??= Drivers::gateway($config);
        $notifier ??= Drivers::notifier($config);
        $mailer ??= Drivers::mailer($config);

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
            $notifier,
            $mailer,
            new BookingFlow($reservations, $repository, $settings, $gateway, $mailer, $logger),
            new BookingToken($config->appKey()),
            new AdminAuth($pdo, $clock, $config->sessionIdleMinutes()),
            new LoginThrottle($pdo),
            new RateLimiter($pdo),
        );
    }
}
