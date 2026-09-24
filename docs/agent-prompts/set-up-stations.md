# Prompt: set up the front desk PC and the gaming PCs

Run this on each PC, with an agent that can use a terminal and the Windows desktop there.

```
Set up the in-venue session control from https://github.com/<owner>/<repo> on this Windows PC. This PC is the <front desk PC / gaming PC for station N>.

Read AGENTS.md and docs/in-venue.md first, then:
1. If msbuild is missing, install Visual Studio 2022 Build Tools with "Desktop development with C++". Ask me before installing anything.
2. Front desk PC: build master-controller/MasterController.sln (Release, x64), run MasterController.exe, start its server, and allow it through Windows Firewall for private networks only. Tell me this PC's LAN IP address.
3. Gaming PC: build station/StationApp.sln (Release, x64), copy x64\Release to C:\ArcadeStation, add a shortcut to the Startup folder, start SteamVR and then StationApp.exe, and connect it to the front desk IP I give you as "Station <N>".
4. Check that the station appears in the master controller. Start a 1 minute session there and confirm the countdown shows on the station and ends by itself.

Rules: never open port 12345 to the internet and never change router settings; do not install anything without asking; at the end report the PC's IP, the station name and anything that did not work.
```
