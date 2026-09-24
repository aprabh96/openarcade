# Station app

Runs on each gaming PC. It connects to the master controller at the front desk, shows the session
countdown and a game menu inside the headset (a SteamVR overlay), launches Steam and custom games,
and ends the session when time is up. [`../docs/in-venue.md`](../docs/in-venue.md) explains how it
fits with the rest of the system.

## Requirements

Windows 10 or 11, SteamVR and a VR headset. To build: Visual Studio 2022 or its Build Tools with the
"Desktop development with C++" workload. Valve's OpenVR SDK is included under `third_party/openvr`
(BSD-3-Clause), so nothing else is needed.

## Build

Open `StationApp.sln` and build **Release | x64**, or from a Developer Command Prompt:

```
msbuild StationApp.sln -p:Configuration=Release -p:Platform=x64
```

The result is `x64\Release\StationApp.exe` with `openvr_api.dll` beside it. Copy that folder to each
gaming PC.

## Set up a station

1. Start SteamVR, then `StationApp.exe`. The first run creates `settings.ini`, `custom_games.json`
   and a `game_cache` folder next to the exe (see `settings.example.ini`).
2. Enter the front desk PC's LAN address and a station name such as `Station 3`, and connect. It
   reconnects by itself from then on.
3. Installed Steam games are found automatically. Add non-Steam games in the app window; they are
   saved in `custom_games.json`.
4. Arrange the games into categories for the in-headset menu.
5. Optional: put a looping video at `media\logo-loop.mp4` to show your branding behind the menu.
6. Put a shortcut to `StationApp.exe` in the Windows Startup folder so it starts with the PC.

**Export game library** writes a web page of your catalog (`game_library.html` plus images) for your
website. The log is `log.txt` next to the exe, trimmed at 50 MB.
