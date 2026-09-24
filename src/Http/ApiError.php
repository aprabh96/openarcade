<?php

declare(strict_types=1);

namespace OpenArcade\Http;

/** An error the client is meant to see. The message is safe to show; internals never go in here. */
final class ApiError extends \RuntimeException
{
    /** @param array<string,mixed> $details */
    public function __construct(
        public readonly string $errorCode,
        string $message,
        public readonly int $status,
        public readonly array $details = [],
    ) {
        parent::__construct($message);
    }

    /** @param array<string,string> $fieldErrors */
    public static function validation(array $fieldErrors, string $message = 'Some fields are not valid.'): self
    {
        return new self('validation_failed', $message, 422, ['fields' => $fieldErrors]);
    }

    public static function badRequest(string $message): self
    {
        return new self('bad_request', $message, 400);
    }

    public static function unauthenticated(): self
    {
        return new self('unauthenticated', 'Sign in to continue.', 401);
    }

    public static function forbidden(string $message): self
    {
        return new self('forbidden', $message, 403);
    }

    public static function notFound(string $message = 'Not found.'): self
    {
        return new self('not_found', $message, 404);
    }

    public static function conflict(string $code, string $message): self
    {
        return new self($code, $message, 409);
    }

    public static function rateLimited(): self
    {
        return new self('rate_limited', 'Too many requests. Please wait a moment and try again.', 429);
    }

    public function toResponse(): Response
    {
        return Response::error($this->errorCode, $this->getMessage(), $this->status, $this->details);
    }
}
