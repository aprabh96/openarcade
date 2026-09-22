<?php

declare(strict_types=1);

use ArcadeOS\Http\App;
use ArcadeOS\Http\NativeSession;
use ArcadeOS\Http\Request;
use ArcadeOS\Http\Response;
use ArcadeOS\Support\Config;

$root = dirname(__DIR__);
require $root . '/vendor/autoload.php';

$path = (string) (parse_url((string) ($_SERVER['REQUEST_URI'] ?? '/'), PHP_URL_PATH) ?: '/');
if ($path === '/' || $path === '') {
    Response::redirect('/book/')->send();

    return;
}
if (!str_starts_with($path, '/api/')) {
    Response::error('not_found', 'No such page.', 404)->send();

    return;
}

$config = Config::load($root);
$secure = (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== '' && $_SERVER['HTTPS'] !== 'off')
    || ($config->trustProxy() && strtolower((string) ($_SERVER['HTTP_X_FORWARDED_PROTO'] ?? '')) === 'https');
$request = Request::fromGlobals(new NativeSession($secure), $config->trustProxy());
App::fromConfig($config)->handle($request)->send();
