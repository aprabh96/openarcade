<?php

declare(strict_types=1);

namespace ArcadeOS\Payments;

use ArcadeOS\Support\HttpClient;
use ArcadeOS\Support\Logger;

/**
 * Square Payments API over plain HTTPS. The amount always comes from the server, the idempotency
 * key is the reservation uuid, and the confirmation code travels as reference_id so a payment can
 * be found again if the response was lost.
 */
final class SquareGateway implements PaymentGateway
{
    public const API_VERSION = '2025-01-23';

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
     * One retry with the same idempotency key, so a request that did reach Square is never charged twice.
     *
     * @param array<string,mixed> $payload
     * @return array{status:int, body:string}|null null when both attempts failed to complete
     */
    private function postWithRetry(string $path, array $payload): ?array
    {
        foreach ([1, 2] as $attempt) {
            try {
                return $this->send('POST', $path, $payload);
            } catch (\RuntimeException $error) {
                $this->logger->warning('square request failed', ['path' => $path, 'attempt' => $attempt, 'error' => $error->getMessage()]);
            }
        }

        return null;
    }

    public function findByReference(string $referenceCode, \DateTimeImmutable $notBefore): ?PaymentResult
    {
        $query = http_build_query([
            'location_id' => $this->locationId,
            'begin_time' => $notBefore->setTimezone(new \DateTimeZone('UTC'))->format('Y-m-d\TH:i:s\Z'),
            'sort_order' => 'DESC',
            'limit' => 100,
        ]);
        try {
            $response = $this->send('GET', '/v2/payments?' . $query, null);
        } catch (\RuntimeException $error) {
            $this->logger->warning('square payment lookup failed', ['error' => $error->getMessage()]);

            return null;
        }
        if ($response['status'] !== 200) {
            return null;
        }
        $data = json_decode($response['body'], true);
        foreach (is_array($data) ? ($data['payments'] ?? []) : [] as $payment) {
            if (is_array($payment) && ($payment['reference_id'] ?? null) === $referenceCode) {
                return ($payment['status'] ?? '') === 'COMPLETED'
                    ? PaymentResult::paid((string) $payment['id'])
                    : PaymentResult::unknown('Payment ' . (string) ($payment['status'] ?? 'unknown'));
            }
        }

        return null;
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

        if ($response['status'] >= 500 || $response['status'] === 429 || $response['status'] === 408) {
            $this->logger->warning('square unavailable', ['status' => $response['status'], 'code' => $code]);

            return PaymentResult::unknown('The payment service is temporarily unavailable.');
        }
        if ($category === 'PAYMENT_METHOD_ERROR' || isset(self::DECLINE_MESSAGES[$code])) {
            return PaymentResult::declined(self::DECLINE_MESSAGES[$code] ?? 'The card was declined.');
        }
        // Any other 4xx means Square rejected the request without charging: our configuration or input is wrong.
        $this->logger->error('square rejected payment request', ['status' => $response['status'], 'code' => $code, 'category' => $category]);

        return PaymentResult::declined('The payment could not be processed. Please try again or pay at the venue.');
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
