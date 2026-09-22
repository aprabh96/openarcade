# Copy-paste prompts for AI agents

Each file below is a prompt you paste into an AI coding agent (Codex, Claude Code, Cursor, Copilot
and similar) that has access to the machine or hosting account where the booking system should
run. The agent reads `AGENTS.md`, asks you a few questions, does the work, and proves it with
`php bin/console doctor`.

| Situation | Prompt |
| --- | --- |
| A server or laptop with Docker | [install-docker.md](install-docker.md) |
| Shared web hosting (cPanel, Plesk, DirectAdmin, FTP only) | [install-shared-hosting.md](install-shared-hosting.md) |
| Set opening hours, prices, stations, branding | [configure-venue.md](configure-venue.md) |
| Charge cards online with Square | [connect-square.md](connect-square.md) |
| Put the booking page on an existing website | [embed-booking-page.md](embed-booking-page.md) |
| Final checks before taking real bookings | [go-live-checklist.md](go-live-checklist.md) |
| Update to a newer release | [upgrade.md](upgrade.md) |
| Make the stations react to the dashboard | [connect-stations.md](connect-stations.md) |

Before pasting a prompt, replace anything in `<angle brackets>`. Never paste passwords or API
tokens into a chat with an agent; put them in `.env` yourself when the agent asks, or type them
into the terminal prompt the agent opens.
