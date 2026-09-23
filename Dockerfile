FROM php:8.2-apache
# The official image ships curl, mbstring, openssl and json built in; pdo_mysql is the one extension
# to add. The php -r line makes the build fail loudly if a future base image drops any of them.
RUN apt-get update && apt-get install -y --no-install-recommends git unzip \
 && docker-php-ext-install pdo_mysql \
 && a2enmod rewrite headers \
 && rm -rf /var/lib/apt/lists/* \
 && php -r 'foreach (["pdo_mysql", "mbstring", "curl", "openssl", "json"] as $e) { if (!extension_loaded($e)) { fwrite(STDERR, "missing PHP extension: $e\n"); exit(1); } }'
COPY --from=composer:2 /usr/bin/composer /usr/bin/composer
ENV APACHE_DOCUMENT_ROOT=/var/www/app/public
RUN sed -ri 's!/var/www/html!${APACHE_DOCUMENT_ROOT}!g' /etc/apache2/sites-available/*.conf /etc/apache2/apache2.conf /etc/apache2/conf-available/*.conf \
 && printf '<Directory /var/www/app/public>\n    AllowOverride All\n    Require all granted\n</Directory>\n' > /etc/apache2/conf-available/app.conf \
 && a2enconf app
WORKDIR /var/www/app
