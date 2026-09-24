<?php

declare(strict_types=1);

namespace OpenArcade\Auth;

final class AdminUser
{
    public function __construct(public readonly int $id, public readonly string $username)
    {
    }
}
