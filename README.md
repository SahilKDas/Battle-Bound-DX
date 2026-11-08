# Battle Bound DX

Native C++26 research port of Battle Bound by @KIKOKO_, with a compact runtime
for the original scripts and assets. Current gameplay is offline.

```powershell
.\tools\build.ps1 -Test -Preview
.\bin\battle_bound_dx.exe --game
```

- [Build, single-EXE packaging, limitations and workspace budget](docs/NATIVE_PORT.md)
- [Clean C++ migration brainstorm](docs/CPP_MIGRATION.md)
- [Repeatable traces, typed content and named assets](docs/REPLAY_AND_CONTENT.md)
- [Typed input and camera systems](docs/INPUT_AND_CAMERA.md)

- [Original game architecture](docs/SCRATCH_ARCHITECTURE.md)
- [Credits, licensing and provenance](CREDITS.md)
- [Local asset browser](scratch/assets.html)
- [Item definitions extracted from the original UI](scratch/items.json)
- [Full source inventory](scratch/inventory.json)

Rebuild the extracted reference with `python tools/unpack_scratch.py`.
The original archive was deleted after extraction verification at the user's request.
Original project metadata remains in `scratch/original/project.json`; named
source assets live in `assets/source/` with their identities in `assets/manifest.json`. Workspace tools enforce
a conservative 450 MB limit beneath the requested 500 MB ceiling; builds stay
in the system temporary directory.

The generated `bin/battle_bound_dx.exe` contains its runtime assets and native
dependencies. Double-click it for a windowed 1280×720 launch. Current target:
Windows 10/11 x64 with a Vulkan-capable graphics driver. The developer build is unsigned.

Rendering now uses Vulkan textured quads and embedded SPIR-V shaders. The
Vulkan backend passes the native checks, isolated single-file gameplay test and
1280×720 rendering/capture check. See `docs/VALIDATION.md` for the test scope.

## Two-player LAN match

Use the same DX build on both computers. On the host, run:

```powershell
./bin/battle_bound_dx.exe --host 37026 --username Host
```

On the guest, replace the address below with the host's IPv4 address:

```powershell
./bin/battle_bound_dx.exe --join 192.168.1.20 37026 --username Guest
```

Use 127.0.0.1 for two processes on the same computer. The host waits up to
30 seconds for one guest. Both processes enter the match on connection.
The direct TCP connection is intended for trusted peers on a local network.
It uses no Scratch cloud service or matchmaking.

Run the integration check with `python tools/test_multiplayer.py bin/battle_bound_dx.exe`.
For CTest, configure with `-DBBDX_NETWORK_TESTS=ON`. The check starts two
processes and requires permission to bind and connect on localhost.

## Frame rate

The simulation runs at 30 ticks per second. Press **F7** to cycle the render
cap through 30, 60, 120, 144, and Unlimited. Press **F8** to toggle VSync.
The game starts at a 60 FPS cap. Use `--fps 144` to set a launch cap or
`--fps 0` for Unlimited. Add `--vsync` to start with VSync enabled.
The Vulkan device can fall back to FIFO when it offers no other present mode.

From the title screen, choose **Host** or **Join** to open the LAN lobby.
The host opens a port and the guest enters the host's IPv4 address and port.
Once connected, both players choose **Ready up**. The match starts when both
are ready. Connection errors appear in the lobby, where you can retry or go
back. The command-line options above still support direct sessions.
