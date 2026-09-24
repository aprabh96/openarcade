<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

use ArcadeOS\Mail\LogMailer;
use ArcadeOS\Mail\Mailer;
use ArcadeOS\Mail\PhpMailerMailer;
use ArcadeOS\Payments\NullGateway;
use ArcadeOS\Payments\PaymentGateway;
use ArcadeOS\Payments\SquareGateway;
use ArcadeOS\Support\Config;
use ArcadeOS\Support\CurlHttpClient;
use ArcadeOS\Support\HttpClient;
use ArcadeOS\Support\Logger;

/** Picks the payment and mail implementations named in .env. */
final class Drivers
{
    public static function gateway(Config $config, Logger $logger, ?HttpClient $http = null): PaymentGateway
    {
        if ($config->paymentMode() === 'none') {
            return new NullGateway();
        }
        $square = $config->square();

        return new SquareGateway($http ?? new CurlHttpClient(), $square['accessToken'], $square['locationId'], $square['environment'], $logger);
    }

    public static function mailer(Config $config, Logger $logger): Mailer
    {
        return match ($config->mailDriver()) {
            'log' => new LogMailer($config->rootDir() . '/storage/logs/mail.log'),
            'mail' => new PhpMailerMailer(null, self::from($config), $config->mailFromName(), $logger),
            default => new PhpMailerMailer($config->smtp(), self::from($config), $config->mailFromName(), $logger),
        };
    }

    private static function from(Config $config): string
    {
        $from = $config->mailFromAddress();
        if (filter_var($from, FILTER_VALIDATE_EMAIL) === false) {
            throw new \RuntimeException('MAIL_FROM_ADDRESS must be a valid email address when MAIL_DRIVER is mail or smtp.');
        }

        return $from;
    }
}
