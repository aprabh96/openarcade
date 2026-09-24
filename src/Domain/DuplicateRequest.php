<?php

declare(strict_types=1);

namespace OpenArcade\Domain;

/** A reservation with the same request id already exists; the caller should return that one. */
final class DuplicateRequest extends \RuntimeException
{
}
