<?php

declare(strict_types=1);

namespace OpenArcade\Mail;

/** Development driver: writes each email to a file (or keeps it in memory) instead of sending it. */
final class LogMailer implements Mailer
{
    /** @var array<int, array{to:string,name:string,subject:string,text:string}> */
    public array $sent = [];

    public function __construct(private ?string $file = null)
    {
    }

    public function send(string $toAddress, string $toName, string $subject, string $text): bool
    {
        $this->sent[] = ['to' => $toAddress, 'name' => $toName, 'subject' => $subject, 'text' => $text];
        if ($this->file === null) {
            return true;
        }
        $entry = sprintf("=== %s\nTo: %s <%s>\nSubject: %s\n\n%s\n\n", gmdate('Y-m-d\TH:i:s\Z'), $toName, $toAddress, $subject, $text);

        return @file_put_contents($this->file, $entry, FILE_APPEND | LOCK_EX) !== false;
    }
}
