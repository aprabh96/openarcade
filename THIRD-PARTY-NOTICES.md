# Third-party notices

This project is MIT licensed (see LICENSE). It uses:

| Component | License | Use |
| --- | --- | --- |
| [PHPMailer](https://github.com/PHPMailer/PHPMailer) | LGPL-2.1 | Sending email over SMTP or PHP `mail()` (runtime dependency, unmodified, loaded through Composer) |
| [PHPUnit](https://phpunit.de/) | BSD-3-Clause | Tests (development only) |
| [PHPStan](https://phpstan.org/) | MIT | Static analysis (development only) |
| [PHP CS Fixer](https://cs.symfony.com/) | MIT | Coding standard (development only) |
| [Square Web Payments SDK](https://developer.squareup.com/docs/web-payments/overview) | Square Developer Terms | Loaded in the customer's browser from Square's CDN only when `PAYMENT_MODE=square`; not distributed with this project |
| [MariaDB](https://mariadb.org/) Docker image | GPL-2.0 | Development and Docker deployment database, unmodified |

Exact versions are pinned in `composer.lock`.
