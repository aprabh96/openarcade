<?php

declare(strict_types=1);

namespace OpenArcade\Auth;

use OpenArcade\Http\Session;
use OpenArcade\Support\Clock;
use PDO;

final class AdminAuth
{
    private const ID = 'admin_id';
    private const SEEN = 'admin_seen_at';

    public function __construct(private PDO $pdo, private Clock $clock, private int $idleMinutes)
    {
    }

    /** Verifies the password and starts a fresh session. Returns null on a wrong username or password. */
    public function login(Session $session, string $username, string $password): ?AdminUser
    {
        $statement = $this->pdo->prepare('SELECT id, username, password_hash FROM admins WHERE username = ?');
        $statement->execute([$username]);
        $row = $statement->fetch();
        // Verify against a dummy hash when the user does not exist, so timing does not reveal usernames.
        $hash = $row === false ? '$2y$10$' . str_repeat('0', 53) : (string) $row['password_hash'];
        $ok = password_verify($password, $hash) && $row !== false;
        if (!$ok) {
            return null;
        }
        $now = $this->clock->now();
        if (password_needs_rehash($hash, PASSWORD_DEFAULT)) {
            $this->pdo->prepare('UPDATE admins SET password_hash = ? WHERE id = ?')
                ->execute([password_hash($password, PASSWORD_DEFAULT), (int) $row['id']]);
        }
        $this->pdo->prepare('UPDATE admins SET last_login_at = ? WHERE id = ?')
            ->execute([$now->format('Y-m-d H:i:s'), (int) $row['id']]);

        $session->regenerate();
        $session->set(self::ID, (int) $row['id']);
        $session->set(self::SEEN, $now->getTimestamp());
        Csrf::rotate($session);

        return new AdminUser((int) $row['id'], (string) $row['username']);
    }

    public function logout(Session $session): void
    {
        $session->destroy();
    }

    /** The signed-in admin, or null when nobody is signed in or the session sat idle too long. */
    public function current(Session $session): ?AdminUser
    {
        $id = $session->get(self::ID);
        $seen = $session->get(self::SEEN);
        if (!is_int($id) || !is_int($seen)) {
            return null;
        }
        $now = $this->clock->now()->getTimestamp();
        if ($now - $seen > $this->idleMinutes * 60) {
            $session->destroy();

            return null;
        }
        $statement = $this->pdo->prepare('SELECT id, username FROM admins WHERE id = ?');
        $statement->execute([$id]);
        $row = $statement->fetch();
        if ($row === false) {
            $session->destroy();

            return null;
        }
        $session->set(self::SEEN, $now);

        return new AdminUser((int) $row['id'], (string) $row['username']);
    }
}
