# vr-arcade-os (working name)

Self-hosted reservation system for VR arcades and other venues that rent numbered stations by the hour. Work in progress. See `docs/superpowers/specs/` for the design.

## Development

    docker compose build
    docker compose run --rm app composer install
    docker compose run --rm app composer check
