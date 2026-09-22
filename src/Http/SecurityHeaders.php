<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

final class SecurityHeaders
{
    /** @param string[] $embedOrigins origins allowed to frame the booking page, in addition to the site itself */
    public function __construct(private array $embedOrigins)
    {
    }

    public function apply(Response $response): Response
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

        return $response
            ->withHeader('Content-Security-Policy', $csp)
            ->withHeader('X-Content-Type-Options', 'nosniff')
            ->withHeader('Referrer-Policy', 'strict-origin-when-cross-origin')
            ->withHeader('Cache-Control', 'no-store');
    }
}
