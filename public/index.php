<?php

declare(strict_types=1);

use ArcadeOS\Http\App;
use ArcadeOS\Http\NativeSession;
use ArcadeOS\Http\Request;
use ArcadeOS\Http\Response;
use ArcadeOS\Support\Config;

$root = dirname(__DIR__);
$path = (string) (parse_url((string) ($_SERVER['REQUEST_URI'] ?? '/'), PHP_URL_PATH) ?: '/');

// PHP's built-in server (development): let it serve real files itself.
if (PHP_SAPI === 'cli-server' && $path !== '/') {
    $file = realpath(__DIR__ . $path);
    if ($file !== false && is_file($file) && str_starts_with($file, __DIR__ . DIRECTORY_SEPARATOR)) {
        return false;
    }
}

require $root . '/vendor/autoload.php';

if ($path === '/' || $path === '') {
    Response::redirect('/book/')->send();

    return;
}
$handled = str_starts_with($path, '/api/') || in_array(rtrim($path, '/'), ['/book', '/admin'], true);
if (!$handled) {
    Response::error('not_found', 'No such page.', 404)->send();

    return;
}

$config = Config::load($root);
$secure = (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== '' && $_SERVER['HTTPS'] !== 'off')
    || ($config->trustProxy() && strtolower((string) ($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '')) === 'https');
$request = Request::fromGlobals(new NativeSession($secure), $config->trustProxy());
App::fromConfig($config)->handle($request)->send();
