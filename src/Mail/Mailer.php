<?php

declare(strict_types=1);

namespace ArcadeOS\Mail;

interface Mailer
{
    /**
     * Sends one plain-text email. Implementations must not throw for delivery problems;
     * they return false and the caller logs it, because a lost email must never lose a booking.
     */
    public function send(string $toAddress, string $toName, string $subject, string $text): bool;
}
