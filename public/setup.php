<?php

declare(strict_types=1);

/*
 * One-time installer for shared hosting without SSH.
 *
 * It only works while SETUP_TOKEN is set in .env, the same token is typed into the form, and no admin
 * account exists yet. After a successful install it refuses to run again; remove SETUP_TOKEN anyway.
 */

use ArcadeOS\Console\Installer;
use ArcadeOS\Db\Connection;
use ArcadeOS\Support\Config;
use ArcadeOS\Support\SystemClock;

$root = dirname(__DIR__);
require $root . '/vendor/autoload.php';

header('Content-Type: text/html; charset=utf-8');
header('Cache-Control: no-store');
header('X-Robots-Tag: noindex');
header("Content-Security-Policy: default-src 'none'; style-src 'self'; form-action 'self'; base-uri 'none'");
header('Referrer-Policy: no-referrer');

$e = static fn (string $text): string => htmlspecialchars($text, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8');
$page = static function (string $title, string $body, int $status = 200) use ($e): void {
    http_response_code($status);
    echo '<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
        . '<title>' . $e($title) . '</title><link rel="stylesheet" href="/assets/app.css"></head>'
        . '<body><main class="card setup"><h1>' . $e($title) . '</h1>' . $body . '</main></body></html>';
};

$config = Config::load($root);
$token = $config->setupToken();
$provided = (string) ($_POST['token'] ?? '');
if ($token === null || $token === '' || strlen($token) < 16) {
    $page('Setup is disabled', '<p>To run setup, put a long random <code>SETUP_TOKEN</code> in <code>.env</code> and reload this page.</p>', 403);

    return;
}
if (!hash_equals($token, $provided)) {
    // The token is typed into a form, never put in the URL, so it stays out of access logs and history.
    $wrong = $provided !== '' ? '<p class="notice error" role="alert">That token does not match SETUP_TOKEN.</p>' : '';
    $page('Set up your booking system', $wrong
        . '<form method="post" action="/setup.php"><div class="field"><label for="token">Setup token (the SETUP_TOKEN value from .env)</label>'
        . '<input type="password" id="token" name="token" required autocomplete="off"></div><button type="submit" class="btn">Continue</button></form>', $provided !== '' ? 403 : 200);

    return;
}

try {
    $pdo = Connection::fromConfig($config);
} catch (\Throwable $error) {
    $page('Database connection failed', '<p class="notice error">' . $e($error->getMessage()) . '</p><p>Check <code>DB_HOST</code>, <code>DB_NAME</code>, <code>DB_USER</code> and <code>DB_PASSWORD</code> in <code>.env</code>, then reload.</p>', 500);

    return;
}

$installer = new Installer($pdo, $root . '/migrations', new SystemClock());
if ($installer->hasAdmin()) {
    $page('Already installed', '<p>An admin account exists, so setup will not run again.</p><p>Remove <code>SETUP_TOKEN</code> from <code>.env</code>. Sign in at <a href="/admin/">/admin/</a>.</p>', 403);

    return;
}

$values = ['venue' => 'My VR Arcade', 'timezone' => 'America/Chicago', 'stations' => '4', 'username' => 'owner'];
$errors = [];
if (isset($_POST['venue'])) {
    foreach (array_keys($values) as $key) {
        $values[$key] = trim((string) ($_POST[$key] ?? ''));
    }
    $password = (string) ($_POST['password'] ?? '');
    $confirm = (string) ($_POST['password_confirm'] ?? '');
    if ($values['venue'] === '' || mb_strlen($values['venue']) > 80) {
        $errors[] = 'Venue name must be 1 to 80 characters.';
    }
    if (!in_array($values['timezone'], \DateTimeZone::listIdentifiers(), true)) {
        $errors[] = 'Choose a valid timezone.';
    }
    if (preg_match('/^\d{1,3}$/', $values['stations']) !== 1 || (int) $values['stations'] < 1 || (int) $values['stations'] > 200) {
        $errors[] = 'Stations must be a number from 1 to 200.';
    }
    if (!Installer::validUsername($values['username'])) {
        $errors[] = 'Username must be 3 to 50 letters, digits, dot, dash or underscore.';
    }
    if (!Installer::validPassword($password)) {
        $errors[] = 'Password must be at least 12 characters.';
    }
    if ($password !== $confirm) {
        $errors[] = 'The two passwords do not match.';
    }
    if ($errors === []) {
        try {
            $lines = $installer->install($values['username'], $password, [
                'venue' => $values['venue'],
                'timezone' => $values['timezone'],
                'stations' => (int) $values['stations'],
            ]);
            $list = '';
            foreach ($lines as $line) {
                $list .= '<li>' . $e($line) . '</li>';
            }
            $page('Installed', '<ul>' . $list . '</ul>'
                . '<p class="notice success"><strong>Now remove <code>SETUP_TOKEN</code> from <code>.env</code>.</strong> This page will not run again.</p>'
                . '<p>Booking page: <a href="/book/">/book/</a><br>Staff dashboard: <a href="/admin/">/admin/</a></p>');

            return;
        } catch (\InvalidArgumentException|\PDOException $error) {
            $errors[] = $error->getMessage();
        }
    }
}

$options = '';
foreach (\DateTimeZone::listIdentifiers() as $zone) {
    $options .= '<option value="' . $e($zone) . '"' . ($zone === $values['timezone'] ? ' selected' : '') . '>' . $e($zone) . '</option>';
}
$errorHtml = '';
if ($errors !== []) {
    $errorHtml = '<div class="notice error" role="alert"><ul>';
    foreach ($errors as $message) {
        $errorHtml .= '<li>' . $e($message) . '</li>';
    }
    $errorHtml .= '</ul></div>';
}
$page('Set up your booking system', $errorHtml
    . '<form method="post" action="/setup.php"><input type="hidden" name="token" value="' . $e($provided) . '">'
    . '<div class="field"><label for="venue">Venue name</label><input type="text" id="venue" name="venue" required maxlength="80" value="' . $e($values['venue']) . '"></div>'
    . '<div class="field"><label for="timezone">Timezone</label><select id="timezone" name="timezone">' . $options . '</select></div>'
    . '<div class="field"><label for="stations">Number of stations</label><input type="number" id="stations" name="stations" min="1" max="200" required value="' . $e($values['stations']) . '"></div>'
    . '<div class="field"><label for="username">Admin username</label><input type="text" id="username" name="username" required maxlength="50" autocomplete="username" value="' . $e($values['username']) . '"></div>'
    . '<div class="field"><label for="password">Admin password (12+ characters)</label><input type="password" id="password" name="password" required minlength="12" autocomplete="new-password"></div>'
    . '<div class="field"><label for="password_confirm">Repeat the password</label><input type="password" id="password_confirm" name="password_confirm" required minlength="12" autocomplete="new-password"></div>'
    . '<button type="submit" class="btn">Install</button></form>'
    . '<p class="small muted">Creates the database tables, default hours (10:00 to 22:00) and prices, and the first admin. You can change everything in the dashboard afterwards.</p>');
