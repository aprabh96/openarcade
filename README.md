# vr-arcade-os (working name)

Self-hosted reservation system for VR arcades and other venues that rent numbered stations by the hour. Work in progress. See `docs/superpowers/specs/` for the design.

## Development

    docker compose build
    docker compose run --rm app composer install
    docker compose run --rm app composer check      # style, static analysis, tests, clean-repo gate

    # try it
    docker compose run --rm -e ARCADEOS_ADMIN_PASSWORD=local-dev-password-123 app php bin/console install --admin-user=owner
    docker compose run --rm app php bin/console seed:demo

`composer check` must pass before every commit. The clean-repo gate (`bin/check-clean`) fails on API keys, real email addresses and phone numbers. Test data uses `@example.com` and `555-01xx` only.
