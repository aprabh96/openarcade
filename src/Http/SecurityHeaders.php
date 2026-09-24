<?php

declare(strict_types=1);

namespace OpenArcade\Http;

final class SecurityHeaders
{
    /** @param string[] $embedOrigins origins allowed to frame the booking page, in addition to the site itself */
    public function __construct(private array $embedOrigins)
    {
    }

    /** @param bool $secure whether the request arrived over HTTPS; only then is HSTS sent */
    public function apply(Response $response, bool $secure = false): Response
    {
        $ancestors = "'self'" . ($this->embedOrigins === [] ? '' : ' ' . implode(' ', $this->embedOrigins));
        $csp = implode('; ', [
            "default-src 'self'",
            "script-src 'self' https://*.squarecdn.com",
            "style-src 'self' 'unsafe-inline'",
            "img-src 'self' data: https:",
            "font-src 'self'",
            "connect-src 'self' https://*.squareup.com https://*.squareupsandbox.com https://*.squarecdn.com",
            'frame-src https://*.squareup.com https://*.squareupsandbox.com https://*.squarecdn.com',
            "frame-ancestors {$ancestors}",
            "base-uri 'self'",
            "form-action 'self'",
            "object-src 'none'",
        ]);

        $response = $response
            ->withHeader('Content-Security-Policy', $csp)
            ->withHeader('X-Content-Type-Options', 'nosniff')
            ->withHeader('Referrer-Policy', 'strict-origin-when-cross-origin')
            ->withHeader('Permissions-Policy', 'camera=(), microphone=(), geolocation=()')
            ->withHeader('Cache-Control', 'no-store');
        if ($secure) {
            $response = $response->withHeader('Strict-Transport-Security', 'max-age=31536000; includeSubDomains');
        }

        return $response;
    }
}
