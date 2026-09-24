<?php

declare(strict_types=1);

namespace OpenArcade\Mail;

use OpenArcade\Support\Logger;
use PHPMailer\PHPMailer\PHPMailer;

/** Sends through PHPMailer, either an SMTP account or the host's mail() function. */
final class PhpMailerMailer implements Mailer
{
    /** @param array{host:string,port:int,user:string,password:string,encryption:string}|null $smtp null = PHP mail() */
    public function __construct(
        private ?array $smtp,
        private string $fromAddress,
        private string $fromName,
        private Logger $logger,
    ) {
    }

    public function send(string $toAddress, string $toName, string $subject, string $text): bool
    {
        try {
            $mail = new PHPMailer(true);
            $mail->CharSet = PHPMailer::CHARSET_UTF8;
            if ($this->smtp === null) {
                $mail->isMail();
            } else {
                $mail->isSMTP();
                $mail->Host = $this->smtp['host'];
                $mail->Port = $this->smtp['port'];
                $mail->SMTPAuth = $this->smtp['user'] !== '';
                $mail->Username = $this->smtp['user'];
                $mail->Password = $this->smtp['password'];
                $mail->SMTPSecure = match ($this->smtp['encryption']) {
                    'ssl' => PHPMailer::ENCRYPTION_SMTPS,
                    'tls' => PHPMailer::ENCRYPTION_STARTTLS,
                    default => '',
                };
                $mail->SMTPAutoTLS = $this->smtp['encryption'] !== 'none';
                $mail->Timeout = 15;
            }
            $mail->setFrom($this->fromAddress, $this->fromName);
            $mail->addAddress($toAddress, $toName);
            $mail->Subject = $subject;
            $mail->Body = $text;
            $mail->isHTML(false);

            return $mail->send();
        } catch (\Throwable $error) {
            // No addresses in the log: the reservation id is logged by the caller.
            $this->logger->warning('email send failed', ['error' => $error->getMessage()]);

            return false;
        }
    }
}
