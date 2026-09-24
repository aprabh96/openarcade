<?php

declare(strict_types=1);

namespace OpenArcade\Payments;

use OpenArcade\Support\HttpClient;
use OpenArcade\Support\Logger;

/**
 * Square Payments API over plain HTTPS. The amount always comes from the server, the idempotency
 * key is the reservation uuid, and the confirmation code travels as reference_id so a payment can
 * be found again if the response was lost.
 */
final class SquareGateway implements PaymentGateway
{
    public const API_VERSION = '2025-01-23';

    /** How far past the hold's creation a late payment is searched for. */
    private const LOOKUP_WINDOW = '+2 days';
    private const LOOKUP_MAX_PAGES = 20;

    private const DECLINE_MESSAGES = [
        'CARD_DECLINED' => 'The card was declined.',
        'GENERIC_DECLINE' => 'The card was declined.',
        'CVV_FAILURE' => 'The security code (CVV) did not match.',
        'ADDRESS_VERIFICATION_FAILURE' => 'The billing postal code did not match.',
        'INVALID_EXPIRATION' => 'The expiry date is not valid.',
        'CARD_EXPIRED' => 'The card has expired.',
        'INSUFFICIENT_FUNDS' => 'The card has insufficient funds.',
        'INVALID_CARD' => 'The card number is not valid.',
        'INVALID_CARD_DATA' => 'The card details are not valid.',
        'CARD_NOT_SUPPORTED' => 'That card type is not accepted.',
        'TRANSACTION_LIMIT' => 'The amount is outside the card limits.',
        'CARD_DECLINED_VERIFICATION_REQUIRED' => 'The card issuer requires verification. Please try another card.',
    ];

    public function __construct(
        private HttpClient $http,
        private string $accessToken,
        private string $locationId,
        private string $environment,
        private Logger $logger,
    ) {
    }

    public function mode(): string
    {
        return 'square';
    }

    public function charge(int $amountCents, string $currency, string $sourceToken, string $idempotencyKey, string $referenceCode): PaymentResult
    {
        $payload = [
            'idempotency_key' => $idempotencyKey,
            'source_id' => $sourceToken,
            'amount_money' => ['amount' => $amountCents, 'currency' => $currency],
            'location_id' => $this->locationId,
            'reference_id' => $referenceCode,
            'note' => 'Booking ' . $referenceCode,
            'autocomplete' => true,
        ];

        $response = $this->postWithRetry('/v2/payments', $payload);
        if ($response === null) {
            return PaymentResult::unknown('The payment service did not respond.');
        }

        return $this->interpretCharge($response);
    }

    /**
     * One retry with the same idempotency key after a transport failure or a 5xx/429/408 answer,
     * so a request that did reach Square is never charged twice and a blip is not a lost booking.
     *
     * @param array<string,mixed> $payload
     * @return array{status:int, body:string}|null null when both attempts failed to complete
     */
    private function postWithRetry(string $path, array $payload): ?array
    {
        $response = null;
        foreach ([1, 2] as $attempt) {
            try {
                $response = $this->send('POST', $path, $payload);
                if (!self::isTransient($response['status'])) {
                    return $response;
                }
                $this->logger->warning('square answered with a transient error', ['path' => $path, 'attempt' => $attempt, 'status' => $response['status']]);
            } catch (\RuntimeException $error) {
                $response = null;
                $this->logger->warning('square request failed', ['path' => $path, 'attempt' => $attempt, 'error' => $error->getMessage()]);
            }
        }

        return $response;
    }

    private static function isTransient(int $status): bool
    {
        return $status >= 500 || $status === 429 || $status === 408;
    }

    public function findByReference(string $referenceCode, \DateTimeImmutable $notBefore): ?PaymentResult
    {
        $utc = new \DateTimeZone('UTC');
        $begin = $notBefore->setTimezone($utc)->modify('-5 minutes');
        $params = [
            'location_id' => $this->locationId,
            'begin_time' => $begin->format('Y-m-d\TH:i:s\Z'),
            'end_time' => $begin->modify(self::LOOKUP_WINDOW)->format('Y-m-d\TH:i:s\Z'),
            'sort_order' => 'ASC',
            'limit' => 100,
        ];
        for ($page = 1; $page <= self::LOOKUP_MAX_PAGES; $page++) {
            try {
                $response = $this->send('GET', '/v2/payments?' . http_build_query($params), null);
            } catch (\RuntimeException $error) {
                $this->logger->warning('square payment lookup failed', ['error' => $error->getMessage()]);
                throw new PaymentLookupFailed('Square could not be reached: ' . $error->getMessage(), 0, $error);
            }
            if ($response['status'] !== 200) {
                $this->logger->warning('square payment lookup refused', ['status' => $response['status']]);
                throw new PaymentLookupFailed('Square answered HTTP ' . $response['status'] . ' to the payment lookup.');
            }
            $data = json_decode($response['body'], true);
            $data = is_array($data) ? $data : [];
            foreach (is_array($data['payments'] ?? null) ? $data['payments'] : [] as $payment) {
                if (is_array($payment) && ($payment['reference_id'] ?? null) === $referenceCode) {
                    return ($payment['status'] ?? '') === 'COMPLETED'
                        ? PaymentResult::paid((string) $payment['id'])
                        : PaymentResult::unknown('Payment ' . (string) ($payment['status'] ?? 'unknown'));
                }
            }
            $cursor = $data['cursor'] ?? null;
            if (!is_string($cursor) || $cursor === '') {
                return null;
            }
            $params['cursor'] = $cursor;
        }
        throw new PaymentLookupFailed('Square returned more than ' . self::LOOKUP_MAX_PAGES . ' pages of payments; lookup abandoned.');
    }

    public function refund(string $paymentId, int $amountCents, string $currency, string $reason): bool
    {
        try {
            $response = $this->send('POST', '/v2/refunds', [
                'idempotency_key' => 'refund-' . $paymentId,
                'payment_id' => $paymentId,
                'amount_money' => ['amount' => $amountCents, 'currency' => $currency],
                'reason' => mb_substr($reason, 0, 190),
            ]);
        } catch (\RuntimeException $error) {
            $this->logger->error('square refund request failed', ['payment' => $paymentId, 'error' => $error->getMessage()]);

            return false;
        }
        if ($response['status'] !== 200) {
            $this->logger->error('square refund refused', ['payment' => $paymentId, 'status' => $response['status']]);

            return false;
        }

        return true;
    }

    /** @param array{status:int, body:string} $response */
    private function interpretCharge(array $response): PaymentResult
    {
        $data = json_decode($response['body'], true);
        $data = is_array($data) ? $data : [];

        if ($response['status'] === 200 || $response['status'] === 201) {
            $payment = $data['payment'] ?? [];
            $status = is_array($payment) ? (string) ($payment['status'] ?? '') : '';
            $id = is_array($payment) ? (string) ($payment['id'] ?? '') : '';
            if ($status === 'COMPLETED' && $id !== '') {
                return PaymentResult::paid($id);
            }
            $this->logger->warning('square payment not completed', ['status' => $status]);

            return PaymentResult::unknown('Payment status ' . $status);
        }

        $errors = is_array($data['errors'] ?? null) ? $data['errors'] : [];
        $first = is_array($errors[0] ?? null) ? $errors[0] : [];
        $code = (string) ($first['code'] ?? '');
        $category = (string) ($first['category'] ?? '');

        if (self::isTransient($response['status'])) {
            $this->logger->warning('square unavailable', ['status' => $response['status'], 'code' => $code]);

            return PaymentResult::unknown('The payment service is temporarily unavailable.');
        }
        if ($category === 'PAYMENT_METHOD_ERROR' || isset(self::DECLINE_MESSAGES[$code])) {
            return PaymentResult::declined(self::DECLINE_MESSAGES[$code] ?? 'The card was declined.');
        }
        // Anything else is Square refusing the request itself: a revoked token, a wrong location id, a
        // malformed request. Nothing was charged, and it is the venue's configuration that needs fixing,
        // not the customer's card.
        $this->logger->error('square rejected payment request', ['status' => $response['status'], 'code' => $code, 'category' => $category]);

        return PaymentResult::error("Square refused the request (HTTP {$response['status']}" . ($code !== '' ? ", {$code}" : '') . '); check the Square settings.');
    }

    /**
     * @param array<string,mixed>|null $payload
     * @return array{status:int, body:string}
     */
    private function send(string $method, string $path, ?array $payload): array
    {
        $base = $this->environment === 'production' ? 'https://connect.squareup.com' : 'https://connect.squareupsandbox.com';

        return $this->http->request($method, $base . $path, [
            'Authorization' => 'Bearer ' . $this->accessToken,
            'Square-Version' => self::API_VERSION,
            'Content-Type' => 'application/json',
            'Accept' => 'application/json',
        ], $payload === null ? null : json_encode($payload, JSON_THROW_ON_ERROR), 20);
    }
}
