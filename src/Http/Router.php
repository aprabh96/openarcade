<?php

declare(strict_types=1);

namespace ArcadeOS\Http;

final class Router
{
    /** @var array<int, array{method:string, regex:string, handler:callable}> */
    private array $routes = [];

    /**
     * @param string $pattern path with {name} placeholders, for example /api/admin/reservations/{id}
     * @param callable(Request, array<string,string>): Response $handler
     */
    public function add(string $method, string $pattern, callable $handler): void
    {
        $regex = '#^' . preg_replace('/\{([a-z_]+)\}/', '(?P<$1>[^/]+)', $pattern) . '$#';
        $this->routes[] = ['method' => strtoupper($method), 'regex' => $regex, 'handler' => $handler];
    }

    public function dispatch(Request $request): Response
    {
        $pathMatched = false;
        foreach ($this->routes as $route) {
            if (preg_match($route['regex'], $request->path, $matches) !== 1) {
                continue;
            }
            $pathMatched = true;
            if ($route['method'] !== $request->method) {
                continue;
            }
            $params = [];
            foreach ($matches as $key => $value) {
                if (is_string($key)) {
                    $params[$key] = $value;
                }
            }

            return ($route['handler'])($request, $params);
        }

        if ($pathMatched) {
            return Response::error('method_not_allowed', 'That method is not allowed here.', 405);
        }

        return Response::error('not_found', 'No such endpoint.', 404);
    }
}
