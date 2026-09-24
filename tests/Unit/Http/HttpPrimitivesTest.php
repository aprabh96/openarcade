<?php

declare(strict_types=1);

namespace OpenArcade\Tests\Unit\Http;

use OpenArcade\Auth\BookingToken;
use OpenArcade\Auth\Csrf;
use OpenArcade\Http\ApiError;
use OpenArcade\Http\ArraySession;
use OpenArcade\Http\Request;
use OpenArcade\Http\Response;
use OpenArcade\Http\Router;
use OpenArcade\Http\SecurityHeaders;
use OpenArcade\Support\Config;
use OpenArcade\Support\Env;
use OpenArcade\Support\FixedClock;
use PHPUnit\Framework\TestCase;

final class HttpPrimitivesTest extends TestCase
{
    private function request(string $method, string $path, array $body = []): Request
    {
        return new Request($method, $path, [], $body, [], '127.0.0.1', new ArraySession());
    }

    public function testRouterMatchesParamsAndDistinguishes404From405(): void
    {
        $router = new Router();
        $router->add('GET', '/api/things/{id}', fn (Request $r, array $p): Response => Response::json(['id' => $p['id']]));
        self::assertSame(['id' => '42'], $router->dispatch($this->request('GET', '/api/things/42'))->decode());
        self::assertSame(405, $router->dispatch($this->request('POST', '/api/things/42'))->status);
        self::assertSame(404, $router->dispatch($this->request('GET', '/api/things/42/extra'))->status);
        self::assertSame(404, $router->dispatch($this->request('GET', '/api/other'))->status);
    }

    public function testResponseErrorShapeAndHeaders(): void
    {
        $response = ApiError::validation(['email' => 'bad'])->toResponse()->withHeader('X-Test', '1');
        self::assertSame(422, $response->status);
        self::assertSame(['error' => ['code' => 'validation_failed', 'message' => 'Some fields are not valid.', 'details' => ['fields' => ['email' => 'bad']]]], $response->decode());
        self::assertSame('1', $response->headers['X-Test']);
        self::assertSame('application/json; charset=utf-8', $response->headers['Content-Type']);
    }

    public function testRequestFieldAccessors(): void
    {
        $request = $this->request('POST', '/x', ['a' => ' text ', 'n' => '12', 'm' => 7, 'f' => 'yes', 'g' => false, 'arr' => [1], 'x' => '1.5']);
        self::assertSame('text', $request->string('a'));
        self::assertNull($request->string('arr'));
        self::assertSame(12, $request->int('n'));
        self::assertSame(7, $request->int('m'));
        self::assertNull($request->int('x'));
        self::assertNull($request->int('missing'));
        self::assertTrue($request->bool('f'));
        self::assertFalse($request->bool('g'));
        self::assertNull($request->bool('a'));
    }

    public function testCsrfTokensAreSessionBoundAndConstantTime(): void
    {
        $session = new ArraySession();
        $token = Csrf::token($session);
        self::assertSame($token, Csrf::token($session));
        self::assertTrue(Csrf::verify($session, $token));
        self::assertFalse(Csrf::verify($session, strrev($token)));
        self::assertFalse(Csrf::verify($session, null));
        self::assertFalse(Csrf::verify(new ArraySession(), $token));
        self::assertNotSame($token, Csrf::rotate($session));
    }

    public function testBookingTokensAreSignedAndExpire(): void
    {
        $clock = new FixedClock('2026-09-21 14:00:00');
        $keyed = new BookingToken('key-one-0123456789abcdef0123456789abc', $clock);
        $token = $keyed->issue();
        self::assertMatchesRegularExpression('/^\d+\.[a-f0-9]{32}\.[a-f0-9]{64}$/', $token);
        self::assertTrue($keyed->verify($token));
        self::assertFalse((new BookingToken('key-two-0123456789abcdef0123456789abc', $clock))->verify($token), 'another key');
        [$time, $nonce] = explode('.', $token);
        self::assertFalse($keyed->verify($time . '.' . $nonce . '.' . str_repeat('0', 64)), 'forged signature');
        self::assertFalse($keyed->verify(((int) $time + 3600) . '.' . $nonce . '.' . explode('.', $token)[2]), 'altered time');
        self::assertFalse($keyed->verify(null));
        self::assertNotSame($token, $keyed->issue());
        $clock->advanceMinutes(4 * 60 + 1);
        self::assertFalse($keyed->verify($token), 'expired');
    }

    public function testForwardedForTakesTheAddressTheProxyAppended(): void
    {
        $_SERVER = ['REQUEST_METHOD' => 'GET', 'REQUEST_URI' => '/api/venue', 'REMOTE_ADDR' => '10.0.0.2', 'HTTP_X_FORWARDED_FOR' => '1.2.3.4, 198.51.100.7'];
        self::assertSame('198.51.100.7', Request::fromGlobals(new ArraySession(), true)->ip, 'the client-supplied left entry is ignored');
        self::assertSame('10.0.0.2', Request::fromGlobals(new ArraySession(), false)->ip);
        $_SERVER = [];
    }

    public function testSecurityHeadersIncludeEmbedOrigins(): void
    {
        $response = (new SecurityHeaders(['https://venue.example.com']))->apply(Response::json([]));
        self::assertStringContainsString("frame-ancestors 'self' https://venue.example.com", $response->headers['Content-Security-Policy']);
        self::assertSame('no-store', $response->headers['Cache-Control']);
        self::assertArrayNotHasKey('Strict-Transport-Security', $response->headers, 'no HSTS over plain http');
        $https = (new SecurityHeaders([]))->apply(Response::json([]), true);
        self::assertStringStartsWith('max-age=', $https->headers['Strict-Transport-Security']);
        $alone = (new SecurityHeaders([]))->apply(Response::json([]));
        self::assertStringContainsString("frame-ancestors 'self';", $alone->headers['Content-Security-Policy']);
    }

    public function testConfigValidatesDriversAndEmbedOrigins(): void
    {
        $config = new Config(new Env(['EMBED_ALLOWED_ORIGINS' => 'https://a.example.com, http://b.example.com:8080'], false), '/app');
        self::assertSame(['https://a.example.com', 'http://b.example.com:8080'], $config->embedAllowedOrigins());
        self::assertSame('none', $config->paymentMode());
        self::assertSame('log', $config->mailDriver());
        self::assertSame(480, $config->sessionIdleMinutes());
        foreach ([['EMBED_ALLOWED_ORIGINS' => 'javascript:alert(1)'], ['PAYMENT_MODE' => 'paypal']] as $bad) {
            try {
                $c = new Config(new Env($bad, false), '/app');
                isset($bad['PAYMENT_MODE']) ? $c->paymentMode() : $c->embedAllowedOrigins();
                self::fail('expected RuntimeException');
            } catch (\RuntimeException) {
                self::assertTrue(true);
            }
        }
    }
}
