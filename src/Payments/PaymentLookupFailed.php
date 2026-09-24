<?php

declare(strict_types=1);

namespace OpenArcade\Payments;

/**
 * The provider could not say whether a payment exists (network error, HTTP error). Callers must
 * treat this as "try again later", never as "no payment".
 */
final class PaymentLookupFailed extends \RuntimeException
{
}
