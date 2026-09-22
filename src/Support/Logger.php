<?php

declare(strict_types=1);

namespace ArcadeOS\Support;

/**
 * Minimal application log. Never pass names, emails, phones, tokens or card data in $context;
 * refer to reservations by id.
 */
final class Logger
{
    /** @var array<int, array{level:string,message:string,context:array<string,mixed>}> */
    private array $records = [];

    public function __construct(private ?string $file)
    {
    }

    /** @param array<string,mixed> $context */
    public function info(string $message, array $context = []): void
    {
        $this->write('INFO', $message, $context);
    }

    /** @param array<string,mixed> $context */
    public function warning(string $message, array $context = []): void
    {
        $this->write('WARNING', $message, $context);
    }

    /** @param array<string,mixed> $context */
    public function error(string $message, array $context = []): void
    {
        $this->write('ERROR', $message, $context);
    }

    /** @return array<int, array{level:string,message:string,context:array<string,mixed>}> */
    public function records(): array
    {
        return $this->records;
    }

    /** @param array<string,mixed> $context */
    private function write(string $level, string $message, array $context): void
    {
        $this->records[] = ['level' => $level, 'message' => $message, 'context' => $context];
        if (count($this->records) > 200) {
            array_shift($this->records);
        }
        if ($this->file === null) {
            return;
        }
        $line = sprintf(
            "[%s] %s: %s%s\n",
            gmdate('Y-m-d\TH:i:s\Z'),
            $level,
            $message,
            $context === [] ? '' : ' ' . json_encode($context, JSON_UNESCAPED_SLASHES)
        );
        @file_put_contents($this->file, $line, FILE_APPEND | LOCK_EX);
    }
}
