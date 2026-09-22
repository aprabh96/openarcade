<?php

declare(strict_types=1);

namespace ArcadeOS\Tests\Unit\Payments;

use ArcadeOS\Payments\PaymentResult;
use ArcadeOS\Payments\SquareGateway;
use ArcadeOS\Support\Logger;
use ArcadeOS\Tests\Support\FakeHttpClient;
use PHPUnit\Framework\TestCase;

final class SquareGatewayTest extends TestCase
{
    private FakeHttpClient $http;

    private function gateway(string $environment = 'sandbox'): SquareGateway
    {
        $this->http = new FakeHttpClient();

        return new SquareGateway($this->http, 'test-access-token', 'LOC123', $environment, new Logger(null));
    }

    public function testChargeSendsServerAmountIdempotencyKeyAndReference(): void
    {
        $gateway = $this->gateway();
        $this->http->queue(200, ['payment' => ['id' => 'PAY1', 'status' => 'COMPLETED']]);
        $result = $gateway->charge(2734, 'USD', 'cnon:card-nonce', 'uuid-1', 'ABCD2345');
        self::assertSame(PaymentResult::PAID, $result->outcome);
        self::assertSame('PAY1', $result->paymentId);

        $request = $this->http->requests[0];
        self::assertSame('POST', $request['method']);
        self::assertSame('https://connect.squareupsandbox.com/v2/payments', $request['url']);
        self::assertSame('Bearer test-access-token', $request['headers']['Authorization']);
        $body = $this->http->json(0);
        self::assertSame(['amount' => 2734, 'currency' => 'USD'], $body['amount_money']);
        self::assertSame('uuid-1', $body['idempotency_key']);
        self::assertSame('ABCD2345', $body['reference_id']);
        self::assertSame('LOC123', $body['location_id']);
        self::assertTrue($body['autocomplete']);
    }

    public function testProductionUsesTheLiveHost(): void
    {
        $gateway = $this->gateway('production');
        $this->http->queue(200, ['payment' => ['id' => 'PAY1', 'status' => 'COMPLETED']]);
        $gateway->charge(100, 'USD', 't', 'k', 'R');
        self::assertStringStartsWith('https://connect.squareup.com/', $this->http->requests[0]['url']);
    }

    public function testDeclinesAreReportedWithAFriendlyMessage(): void
    {
        $gateway = $this->gateway();
        $this->http->queue(402, ['errors' => [['category' => 'PAYMENT_METHOD_ERROR', 'code' => 'CVV_FAILURE', 'detail' => 'CVV']]]);
        $result = $gateway->charge(100, 'USD', 't', 'k', 'R');
        self::assertSame(PaymentResult::DECLINED, $result->outcome);
        self::assertSame('The security code (CVV) did not match.', $result->message);
        self::assertNull($result->paymentId);
    }

    public function testOurOwnBadRequestIsNotACharge(): void
    {
        $gateway = $this->gateway();
        $this->http->queue(400, ['errors' => [['category' => 'INVALID_REQUEST_ERROR', 'code' => 'BAD_REQUEST']]]);
        self::assertSame(PaymentResult::DECLINED, $gateway->charge(100, 'USD', 't', 'k', 'R')->outcome);
    }

    public function testTransportFailureRetriesOnceWithTheSameKeyThenReportsUnknown(): void
    {
        $gateway = $this->gateway();
        $this->http->queueFailure('timeout')->queue(200, ['payment' => ['id' => 'PAY2', 'status' => 'COMPLETED']]);
        self::assertSame('PAY2', $gateway->charge(100, 'USD', 't', 'key-x', 'R')->paymentId);
        self::assertCount(2, $this->http->requests);
        self::assertSame('key-x', $this->http->json(1)['idempotency_key']);

        $gateway = $this->gateway();
        $this->http->queueFailure('timeout')->queueFailure('timeout again');
        self::assertSame(PaymentResult::UNKNOWN, $gateway->charge(100, 'USD', 't', 'k', 'R')->outcome);
    }

    public function testServerErrorsAndPendingStatusAreUnknown(): void
    {
        $gateway = $this->gateway();
        $this->http->queue(503, ['errors' => [['category' => 'API_ERROR', 'code' => 'SERVICE_UNAVAILABLE']]]);
        self::assertSame(PaymentResult::UNKNOWN, $gateway->charge(100, 'USD', 't', 'k', 'R')->outcome);
        $this->http->queue(200, ['payment' => ['id' => 'PAY3', 'status' => 'PENDING']]);
        self::assertSame(PaymentResult::UNKNOWN, $gateway->charge(100, 'USD', 't', 'k', 'R')->outcome);
    }

    public function testFindByReferenceFiltersTheListing(): void
    {
        $gateway = $this->gateway();
        $this->http->queue(200, ['payments' => [
            ['id' => 'OTHER', 'status' => 'COMPLETED', 'reference_id' => 'ZZZZ9999'],
            ['id' => 'MINE', 'status' => 'COMPLETED', 'reference_id' => 'ABCD2345'],
        ]]);
        $found = $gateway->findByReference('ABCD2345', new \DateTimeImmutable('2026-09-21 14:00:00', new \DateTimeZone('UTC')));
        self::assertSame('MINE', $found?->paymentId);
        self::assertStringContainsString('begin_time=2026-09-21T14%3A00%3A00Z', $this->http->requests[0]['url']);
        self::assertStringContainsString('location_id=LOC123', $this->http->requests[0]['url']);

        $this->http->queue(200, ['payments' => []]);
        self::assertNull($gateway->findByReference('NONE0000', new \DateTimeImmutable('now', new \DateTimeZone('UTC'))));
    }

    public function testRefundPostsFullAmountWithAnIdempotencyKey(): void
    {
        $gateway = $this->gateway();
        $this->http->queue(200, ['refund' => ['id' => 'REF1', 'status' => 'PENDING']]);
        self::assertTrue($gateway->refund('PAY1', 2734, 'USD', 'Time no longer available'));
        $body = $this->http->json(0);
        self::assertSame('refund-PAY1', $body['idempotency_key']);
        self::assertSame(['amount' => 2734, 'currency' => 'USD'], $body['amount_money']);
        $this->http->queue(400, ['errors' => [['code' => 'BAD_REQUEST']]]);
        self::assertFalse($gateway->refund('PAY1', 2734, 'USD', 'x'));
    }
}
