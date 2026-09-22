<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

use ArcadeOS\Mail\LogMailer;
use ArcadeOS\Mail\Mailer;
use ArcadeOS\Payments\NullGateway;
use ArcadeOS\Payments\PaymentGateway;
use ArcadeOS\Realtime\Notifier;
use ArcadeOS\Realtime\NullNotifier;
use ArcadeOS\Support\Config;

/** Picks the payment, real-time and mail implementations named in .env. */
final class Drivers
{
    public static function gateway(Config $config): PaymentGateway
    {
        return match ($config->paymentMode()) {
            'none' => new NullGateway(),
            default => throw new \RuntimeException('PAYMENT_MODE ' . $config->paymentMode() . ' is not available in this build.'),
        };
    }

    public static function notifier(Config $config): Notifier
    {
        return match ($config->realtimeDriver()) {
            'none' => new NullNotifier(),
            default => throw new \RuntimeException('REALTIME_DRIVER ' . $config->realtimeDriver() . ' is not available in this build.'),
        };
    }

    public static function mailer(Config $config): Mailer
    {
        return match ($config->mailDriver()) {
            'log' => new LogMailer($config->rootDir() . '/storage/logs/mail.log'),
            default => throw new \RuntimeException('MAIL_DRIVER ' . $config->mailDriver() . ' is not available in this build.'),
        };
    }
}
