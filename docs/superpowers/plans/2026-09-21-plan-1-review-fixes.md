# Plan 1 review fixes

Findings from the independent review of Plan 1, with the exact changes to make. One commit per fix, TDD where a behaviour changes (write the test, see it fail, then fix).

## Fix 1: late payment has one outcome, whether or not the sweep already ran

Problem: `Reservations::confirmPayment()` handles a past-due hold only when the row is still `held`. If `expireHolds()` already flipped it to `expired`, the same real-world event returns `wrong_status` instead of `hold_expired`.

Rule: a past-due hold (still `held` with an old deadline, or already `expired`) is honoured when all of its stations are still free, and otherwise ends as `expired` with `BookingRejected('hold_expired')` so the caller refunds.

Tests to add to `tests/Integration/ReservationsTest.php` (see them fail first):

```php
    public function testLatePaymentAfterTheSweepIsHonouredWhenStationsAreStillFree(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        self::assertSame(1, $this->service->expireHolds());
        $confirmed = $this->service->confirmPayment($held->id, 'square', 'pay_after_sweep');
        self::assertSame('confirmed', $confirmed->status);
        self::assertSame('pay_after_sweep', $confirmed->paymentId);
    }

    public function testLatePaymentAfterTheSweepIsRefusedWhenStationsWereTaken(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(true));
        $this->clock->advanceMinutes(11);
        self::assertSame(1, $this->service->expireHolds());
        $this->service->create(VenueFixture::request('2026-09-22', 600, 60, 2), BookingRules::customer(false));
        try {
            $this->service->confirmPayment($held->id, 'square', 'pay_too_late');
            self::fail('expected hold_expired');
        } catch (BookingRejected $rejected) {
            self::assertSame('hold_expired', $rejected->reason);
        }
    }

    public function testCancelledReservationCannotBeConfirmed(): void
    {
        $held = $this->service->create(VenueFixture::request('2026-09-22', 600), BookingRules::customer(true));
        $this->service->cancel($held->id);
        try {
            $this->service->confirmPayment($held->id, 'square', 'pay_x');
            self::fail('expected wrong_status');
        } catch (BookingRejected $rejected) {
            self::assertSame('wrong_status', $rejected->reason);
        }
    }
```

Change in `src/Domain/Reservations.php`, inside the `confirmPayment` closure. Replace

```php
            if ($current->status !== 'held') {
                throw new BookingRejected('wrong_status');
            }
            $expired = $current->holdExpiresAtUtc !== null && $current->holdExpiresAtUtc <= $now->format('Y-m-d H:i:s');
```

with

```php
            // A hold can be past due in two ways: still "held" with an old deadline, or already swept to
            // "expired" by expireHolds(). Both are handled the same, so the outcome never depends on
            // whether the sweep happened to run first.
            if (!in_array($current->status, ['held', 'expired'], true)) {
                throw new BookingRejected('wrong_status');
            }
            $expired = $current->status === 'expired'
                || ($current->holdExpiresAtUtc !== null && $current->holdExpiresAtUtc <= $now->format('Y-m-d H:i:s'));
```

In the "stations were taken" branch replace `$this->reservations->transition($id, ['held'], 'expired', $now);` with

```php
                    if ($current->status === 'held') {
                        $this->reservations->transition($id, ['held'], 'expired', $now);
                    }
```

and change the confirming transition to accept both states:

```php
            $this->reservations->transition($id, ['held', 'expired'], 'confirmed', $now, $provider, $paymentId);
```

Update the method docblock to state the rule above. Commit: `Honour or refuse late payments the same way whether or not the sweep ran`.

## Fix 2: the hold sweep retries on deadlock, has an index, and the console reports database errors

- `src/Domain/Reservations.php`: `expireHolds()` becomes
  ```php
  public function expireHolds(): int
  {
      return Transaction::run($this->pdo, fn (): int => $this->reservations->expireHolds($this->clock->now()));
  }
  ```
- `migrations/001_init.sql`: in `reservations`, after `KEY ix_reservations_date_status (local_date, status)` add `KEY ix_reservations_status_hold (status, hold_expires_at)` (mind the commas). Nothing has been released, so editing migration 001 is correct; do not add a migration 002.
- `tests/Integration/MigrationTest.php`: add a test that `SHOW INDEX FROM reservations` contains `ix_reservations_status_hold`.
- `src/Console/Application.php`: in `run()`, after the `InvalidArgumentException` catch add
  ```php
  } catch (\PDOException $error) {
      ($this->write)('Database error: ' . $error->getMessage());

      return 1;
  ```
Commit: `Make the hold sweep retryable and indexed; report database errors in the console`.

## Fix 3: prove confirmPayment under real concurrency

Create `tests/Integration/workers/confirm_worker.php`:

```php
<?php

declare(strict_types=1);

use OpenArcade\Domain\BookingRejected;
use OpenArcade\Domain\Reservations;
use OpenArcade\Support\FixedClock;
use OpenArcade\Tests\Integration\TestDb;

require dirname(__DIR__, 3) . '/vendor/autoload.php';

[, $startAt, $reservationId, $paymentId] = $argv;

$service = Reservations::build(TestDb::connect(), new FixedClock('2026-09-21 14:00:00'));
while (microtime(true) < (float) $startAt) {
    usleep(500);
}
try {
    $reservation = $service->confirmPayment((int) $reservationId, 'square', $paymentId);
    echo json_encode(['result' => $reservation->status, 'payment_id' => $reservation->paymentId]);
} catch (BookingRejected $rejected) {
    echo json_encode(['result' => $rejected->reason]);
} catch (\Throwable $error) {
    echo json_encode(['result' => 'error', 'message' => $error->getMessage()]);
}
```

In `tests/Integration/ConcurrencyTest.php` generalise the private `race()` helper so it takes the worker file name and the argument list, keep the three existing tests passing unchanged in meaning, and add:

- `testSixDuplicateConfirmationsAreAllIdempotent`: seed 1 station, create one held reservation (customer rules, payment required, clock `2026-09-21 14:00:00`), start 6 `confirm_worker.php` processes with the SAME payment id `pay_same`. Assert all six results are `confirmed` with `payment_id` `pay_same`, and the row in the database is `confirmed` with that payment id.
- `testSixDifferentPaymentIdsExactlyOneWins`: same setup, payment ids `pay_0` .. `pay_5`. Assert exactly one `confirmed` and five `wrong_status`, no `error`, and the database `payment_id` equals the winner's.

Run the file five times in a row; all must pass. Commit: `Prove confirmPayment under multi-process concurrency`.

## Fix 4: dependency audit in the gate; generic secret rule; say what the denylist is

- `composer.json` scripts: add `"audit": "@composer audit"` and append `"@audit"` to `check`. `.github/workflows/ci.yml`: add `- run: composer audit` after `composer clean-check`.
- `tools/CleanScanner.php`: add a rule for secrets with no vendor prefix. After the phone check, per line:
  ```php
  private const GENERIC_SECRET = '/(?i)\b[a-z0-9_\-]*(?:password|passwd|secret|token|api[_\-]?key|private[_\-]?key)[a-z0-9_]*[\'"]?\s*(?:=>|=|:)\s*[\'"]?([A-Za-z0-9\/+=_\-]{16,})/';
  private const PLACEHOLDER_HINT = '/(?i)example|local|change|dummy|test|your|placeholder|xxxx/';
  ```
  Report rule `generic_secret` only when the captured value contains at least one digit and one letter and does not match `PLACEHOLDER_HINT`.
- `tests/Unit/Tools/CleanScannerTest.php`: add tests, building fixtures by concatenation: flags `'SMTP_PASSWORD=' . 'k9Xv' . 'Q2mL' . 'p7Rt' . 'z4Wn' . 'b8Hc'`; ignores `OPENARCADE_ADMIN_PASSWORD=local-dev-password-123`; ignores `--admin-password=correct-horse-battery` (no digit); ignores `$password = trim((string) fgets(STDIN));`; ignores `GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}`.
- `bin/check-clean`: add a comment at the top: the hash denylist is a local, git-ignored aid for the maintainer and does nothing in CI or a fresh clone; gitleaks in CI is the broad backstop.
Run the gate; if the new rule flags anything already in the repository, decide case by case: real problem -> fix the file; harmless fixture -> reword it. Do not allowlist. Commit: `Add dependency audit and a generic secret rule to the gate`.

## Fix 5: hide the admin password while it is typed

`src/Console/Application.php`, `adminPassword()`: when prompting on a POSIX terminal, turn echo off for the read and always restore it:

```php
        if ($password === '' && defined('STDIN') && function_exists('posix_isatty') && posix_isatty(STDIN)) {
            ($this->write)('Admin password (12+ characters, input hidden):');
            $saved = shell_exec('stty -g 2>/dev/null');
            $hidden = is_string($saved) && trim($saved) !== '';
            if ($hidden) {
                shell_exec('stty -echo');
            }
            try {
                $password = trim((string) fgets(STDIN));
            } finally {
                if ($hidden) {
                    shell_exec('stty ' . escapeshellarg(trim((string) $saved)));
                    ($this->write)('');
                }
            }
        }
```

`README.md`: under the install example add one sentence: the inline password is for local development only; in production pass it through an environment file or the prompt, never on the command line. Commit: `Hide the admin password prompt`.

## Fix 6: small hardening

- `.gitignore`: replace the line `.env` with `.env*` and add `!.env.example` on the next line.
- Delete `src/.gitkeep`.
- `src/Db/Migrator.php`: extend the `statements()` docblock: the splitter does not support stored procedures, triggers, `DELIMITER` blocks or string literals that end a line with `;`. Keep migrations to plain statements.
- `src/Domain/ReservationRepository.php`:
  - `insert()`: before building SQL, reject unknown columns. Add a private const `INSERTABLE` listing exactly the columns the service sets (`status, first_name, last_name, email, phone, comments, local_date, start_utc, end_utc, duration_minutes, station_count, subtotal_cents, tax_cents, total_cents, currency, hold_expires_at, created_by, created_at, updated_at`) and throw `\InvalidArgumentException` when `array_diff(array_keys($fields), self::INSERTABLE)` is not empty. Do this before `uuid` and `confirmation_code` are added. Add a unit-free integration test in `ReservationsTest` or a new `ReservationRepositoryTest` that an unknown column is rejected.
  - `transition()`: add a comment that `rowCount()` counts changed rows, which is reliable here because every transition changes `status`.
- `tests/Integration/ReservationsTest.php`: add `testSessionEndingAtLocalMidnight`: set Tuesday (weekday 2) hours to 600-1440 with `HoursRepository::setWeekday`, book `2026-09-22` at 1380 for 60 minutes on both stations, assert `endUtc` is `2026-09-23 05:00:00`, then assert a second booking at 1380 is refused with `slot_unavailable` and one at 1290 (21:30-22:30, buffer 10) is also refused because it would run into the 23:00 session's buffer... check this by hand before asserting: 1290+60=1350; conflict when `1290 < 1440+10` and `1380 < 1350+10` -> `1380 < 1360` is false -> NOT a conflict, so a 21:30 booking is allowed. Assert exactly what the rule gives: 1290 succeeds, 1350 (22:30-23:30) is refused.
Commit: `Small hardening from review`.

## Finish

Run the full gate. All green, then report the final counts.
