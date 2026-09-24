# Master controller

Runs on the front desk PC. Every station app connects to it over the local network, and staff use it
to start, extend and stop sessions on one station or a group. [`../docs/in-venue.md`](../docs/in-venue.md)
has the protocol and the security notes.

## Build

Visual Studio 2022 or its Build Tools with "Desktop development with C++". No other dependencies.

```
msbuild MasterController.sln -p:Configuration=Release -p:Platform=x64
```

Run `x64\Release\MasterController.exe` and start the server (port 12345). When Windows asks, allow it
through the firewall for **private** networks only. Stations appear as they connect.
