<?php

declare(strict_types=1);

namespace ArcadeOS\Tools;

final class CleanScanner
{
    private const KEY_RULES = [
        'square_access_token' => '/\bEAAA[A-Za-z0-9_\-]{20,}/',
        'square_application_id' => '/\b(?:sandbox-)?sq0(?:idp|csp|atp)-[A-Za-z0-9_\-]{10,}/',
        'google_api_key' => '/\bAIza[0-9A-Za-z_\-]{30,}/',
        'openai_style_key' => '/\bsk-[A-Za-z0-9_\-]{20,}/',
        'stripe_key' => '/\b[sp]k_(?:live|test)_[A-Za-z0-9]{16,}/',
        'private_key_block' => '/-----BEGIN [A-Z ]*PRIVATE KEY-----/',
        'jwt' => '/\beyJ[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}\.[A-Za-z0-9_\-]{10,}/',
    ];
    private const EMAIL = '/[A-Za-z0-9._%+\-]+@([A-Za-z0-9.\-]+\.[A-Za-z]{2,})/';
    private const PHONE = '/(?<![\d.\-])(?:\+?1[\s\-.])?\(?\d{3}\)?[\s\-.]\d{3}[\s\-.]\d{4}(?![\d\-])/';
    private const FICTIONAL_PHONE = '/555[\s\-.]01\d\d/';
    private const GENERIC_SECRET = '/(?i)\b[a-z0-9_\-]*(?:password|passwd|secret|token|api[_\-]?key|private[_\-]?key)[a-z0-9_]*[\'"]?\s*(?:=>|=|:)\s*[\'"]?([A-Za-z0-9\/+=_\-]{16,})/';
    private const PLACEHOLDER_HINT = '/(?i)example|local|change|dummy|test|your|placeholder|xxxx/';
    private const ALLOWED_EMAIL_DOMAINS = ['example.com', 'example.org', 'example.net', 'arcade.test'];
    private const TOKEN = '/[A-Za-z0-9_\-\.\+\/=\^%\$#@!~\*]{8,}/';

    /**
     * @param string[] $denylistHashes lower-case SHA-256 hex digests of forbidden strings
     * @param string[] $allowlist exact strings that are never reported
     */
    public function __construct(private array $denylistHashes = [], private array $allowlist = [])
    {
        $this->denylistHashes = array_map('strtolower', $this->denylistHashes);
    }

    /** @return Finding[] */
    public function scanText(string $file, string $text): array
    {
        $findings = [];
        $lines = preg_split('/\R/', $text) ?: [];
        foreach ($lines as $index => $line) {
            $lineNo = $index + 1;
            foreach (self::KEY_RULES as $rule => $pattern) {
                if (preg_match_all($pattern, $line, $m)) {
                    foreach ($m[0] as $match) {
                        $this->add($findings, $file, $lineNo, $rule, $match);
                    }
                }
            }
            if (preg_match_all(self::EMAIL, $line, $m, PREG_SET_ORDER)) {
                foreach ($m as $match) {
                    if (!in_array(strtolower($match[1]), self::ALLOWED_EMAIL_DOMAINS, true)) {
                        $this->add($findings, $file, $lineNo, 'email', $match[0]);
                    }
                }
            }
            if (preg_match_all(self::PHONE, $line, $m)) {
                foreach ($m[0] as $match) {
                    if (!preg_match(self::FICTIONAL_PHONE, $match)) {
                        $this->add($findings, $file, $lineNo, 'phone', $match);
                    }
                }
            }
            if (preg_match_all(self::GENERIC_SECRET, $line, $m)) {
                foreach ($m[1] as $captured) {
                    $looksLikeASecret = preg_match('/\d/', $captured) === 1 && preg_match('/[A-Za-z]/', $captured) === 1;
                    if ($looksLikeASecret && preg_match(self::PLACEHOLDER_HINT, $captured) !== 1) {
                        $this->add($findings, $file, $lineNo, 'generic_secret', $captured);
                    }
                }
            }
            if ($this->denylistHashes !== [] && preg_match_all(self::TOKEN, $line, $m)) {
                foreach ($m[0] as $token) {
                    foreach ([$token, trim($token, '.,;:!')] as $candidate) {
                        if (in_array(hash('sha256', $candidate), $this->denylistHashes, true)) {
                            $this->add($findings, $file, $lineNo, 'denylist', $candidate);
                            break;
                        }
                    }
                }
            }
        }

        return $findings;
    }

    /** @param Finding[] $findings */
    private function add(array &$findings, string $file, int $line, string $rule, string $match): void
    {
        if (in_array($match, $this->allowlist, true)) {
            return;
        }
        $length = strlen($match);
        $preview = $length <= 6
            ? str_repeat('*', $length)
            : substr($match, 0, 2) . str_repeat('*', min(12, $length - 4)) . substr($match, -2) . " (len {$length})";

        $findings[] = new Finding($file, $line, $rule, $preview);
    }
}
