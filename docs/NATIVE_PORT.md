# Native C++26 port

The original script graph is compiled into a compact runtime image and executed
by a native C++26 compatibility runtime. This preserves the imported gameplay
logic rather than manually approximating 15,331 Scratch blocks. It is an offline
research port, not a finished DX rewrite or a claim of full Scratch parity.

## Build and run on this workstation

```powershell
.\tools\build.ps1 -Test -Preview
.\bin\battle_bound_dx.exe
```

For a direct offline gameplay entry point:

```powershell
.\bin\battle_bound_dx.exe --game
```

F5 restarts the project; F6 enters offline gameplay. The original configurable
controls remain in use (W/A/D/S, mouse, Space, R, T, M, wheel and number keys).
The default is a windowed 1280×720 client area with a 640×360 logical stage
and 30 simulation steps per second. Rendering uses a full 1280×720 target;
SVG-derived textures are rasterized at 2× resolution with fractional placement
and bilinear filtering. Native text uses the Windows Segoe UI font when available.
Mouse coordinates use the same viewport transform.

The installed GCC 14.1 accepts `-std=c++2c`, C++26 draft mode. This does not
claim complete implementation of the final C++26 standard by that compiler.
Build prerequisites already present here: CMake, Ninja, GCC, raylib 5.5,
Python, PyMuPDF, and the installed Vulkan SDK shader tools. No package download or dependency tree is vendored.
Python is used only to prepare content/build checks; gameplay runs in C++.
The executable statically links raylib, GLFW and the GCC C++ runtime. Its
read-only content package includes the project, all runtime images/sounds, and
license/credit documents. Assets are read from the executable without extraction.
It can be copied alone to another folder or drive and launched by double-click.
No Python, toolchain, asset directory, or extra application DLLs are required.

The current target is **64-bit Windows 10/11**, with a graphics driver supporting
Vulkan 1.0 and the KHR swapchain extension. This is not a universal binary for 32-bit Windows, every historic
Windows version, or every GPU. The developer build is unsigned; publisher or
reputation warnings can still appear. Removing those warnings requires a release
signing process and certificate, not a runtime dependency change.

## Layout

| File | Role |
|---|---|
| `src/runtime.hpp`, `src/runtime.cpp` | Native values, scheduler, actors/clones, variables/lists, procedures, collision, renderer, input and sound |
| `src/main.cpp` | Window/game entry point, headless checks, scripted input and framebuffer capture |
| `tools/compile_project.py` | Original SB3 → numeric opcodes, graph links, IDs, content metadata |
| `scratch/project.bbdx` | Versioned little-endian runtime image, about 1.53 MB |
| `scratch/compile-report.json` | Source SHA-256 and all 107 imported opcodes |
| `tools/rasterize_assets.py` | SVG → PNG preparation; retains original pivots/resolution metadata |
| `assets/raster/` | 765 unique SVG rasterizations at 2× resolution, about 7.08 MB total |
| `tools/workspace_budget.py` | Workspace storage guard |

All imported instructions retain their Scratch block IDs for diagnostics.
Native runtime image loading requires the matching compiler format version.
Original source, comments, credits, and license notices remain available.

## Implemented behavior and deliberate changes

- All 107 opcodes occurring in this project have runtime dispatch paths.
- Custom procedures retain named arguments and warp metadata; early return
  follows the Scratch VM procedure-unwinding behavior.
- Cooperative task scheduling includes loops, waits, broadcasts, wait groups,
  receive-hat restart, stop operations, and clone lifecycle/local state.
- Dynamic values/lists retain ID-based scope, numeric conversion,
  case-insensitive comparison, indexing, and basic string behavior.
- Costume pivots, rotation/mirroring, alpha-mask collision, scrolling map
  chunks, stamps, ghost/color/brightness effects, input and sound are native.
- Source cloud encoding/decoding runs against local cloud-variable storage.
  Stale downloaded cloud snapshots are cleared, and the local version marker
  is seeded to 16 to satisfy the original service availability check.
- `--game` bypasses intro/menu and starts an offline FFA session on map 1.
  It keeps the original world/spawn/match logic, including wall-clock phases.
- Audio is muted by default. `--audio` enables the preserved sound playback
  for inspection; no third-party audio has been marked cleared for release.

The procedure-return behavior was checked against the upstream
[Scratch VM thread implementation](https://github.com/scratchfoundation/scratch-vm/blob/develop/src/engine/thread.js).
This runtime was written locally; it does not bundle the Scratch VM engine.

## Verification and remaining gaps

`-Test` runs value/procedure/loop/clone/broadcast regression checks, original
startup, offline gameplay, and a scripted movement/fire/reload/crouch exercise.
The exercise checks actual world displacement and creation of bullet clones.
Unknown statement opcodes fail loudly; headless runs return failure if scripts
stop unexpectedly. `-Preview` renders the original game through the native
graphics path and saves `scratch/native-preview.png` for visual inspection.

These checks cover selected active paths, not every imported branch or exact
equivalence to Scratch. Remaining gaps include:

- Real multiplayer transport, sessions and synchronization. There is no
  connection to Scratch's cloud service and no replacement server yet.
- Full movement/combat/vehicle/mode parity against side-by-side original runs.
- Exact Scratch scheduler/redraw timing, costume-size/fencing limits,
  UTF-16 string edge cases, and some bitmap filtering/alpha edge behavior.
- Full menu/intro interaction and save/load round-trip testing. Ask dialogs
  and list displays use basic native UI; they are not Scratch editor widgets.
- SVG font/filter differences from rasterization, full sound mixing behavior,
  and asset-specific third-party permissions before distribution.

This architecture provides a native baseline for replacing legacy subsystems
incrementally with ordinary typed C++ DX systems.

## Workspace limit

The user ceiling is **500,000,000 bytes (500 MB)**, using decimal units.
The tools impose a stricter **450,000,000-byte** guard and count files including
hidden files, `.git`, source assets, generated caches and the executable.
Content generation reserves its bounded output allowance before writing;
rasterization has an additional 100 MB reservation/ceiling for each run.

CMake rejects in-repository build directories. `build.ps1` uses one deterministic
temporary build folder per checkout, then copies only the single packaged
executable into ignored `bin/`. No SDK, package cache, object files, debug symbols,
or duplicate asset deployment tree is placed in the repository. Rebuilding
overwrites the same generated content and binary.

Run `python tools/workspace_budget.py` to audit current usage. The guard controls
these tools; arbitrary external file writes or user-created files cannot be
prevented by this repository. Do not create large dependency/build folders here.

## Standalone verification

The build inspects the PE import table and rejects non-system DLL dependencies.
`-Test` additionally copies only the executable into an isolated temporary folder,
removes the toolchain from PATH, and runs the scripted gameplay check without
`--root`. The Vulkan build has passed that isolation check on this
Windows host; this is not a substitute for testing other Windows machines.
The executable uses the Windows GUI subsystem, so double-clicking does not open
a console window. `--credits` can be captured from a command-line process.

See [the migration brainstorm](CPP_MIGRATION.md) for the proposed transition to
clean, typed C++ systems.

## Vulkan renderer

`src/graphics.cpp` owns the window, Vulkan instance/device, swapchain, render
pass, sprite pipeline, descriptors, texture uploads, synchronization and input.
GLFW uses `GLFW_NO_API`, so the application creates no OpenGL context. Raylib
is retained for CPU image decoding, collision helpers and audio only.

The renderer draws alpha-blended textured quads with fractional positions,
rotation/mirroring, ghost/color/brightness effects and bilinear sampling.
UI text uses an antialiased Windows font atlas uploaded to a Vulkan image.
The default remains a windowed 1280×720 viewport with a 640×360 logical space.

GLSL 450 sources live in `src/shaders/`. CMake compiles them with the installed
`glslc`, validates both modules with `spirv-val --target-env vulkan1.0`, and
embeds their SPIR-V in the executable. No shader files or SDK are required on
the target computer. Vulkan entry points are loaded through GLFW from the
system graphics driver; no Vulkan loader DLL is bundled.

The first graphics/presentation-capable queue is used, with a FIFO swapchain
and one frame in flight. Texture uploads currently wait for the queue, favoring
simple ownership over asynchronous upload throughput. Readback capture and normal runtime cleanup have been exercised on this host.
Swapchain recreation still needs a dedicated device test.
The GPU must support Vulkan 1.0, VK_KHR_swapchain and the Windows surface.

Following renewed user authorization, this revision passes **compile, SPIR-V
validation, package/import inspection, four native tests, isolated single-file
gameplay and Vulkan rendering/readback inspection**. The updated
`scratch/native-preview.png` is the 1280×720 Vulkan capture. See
`docs/VALIDATION.md` for scope and remaining coverage gaps. The unsigned publisher
warning remains a signing issue, independent of the API.

## Replay/content milestone

See [repeatable gameplay and typed content](REPLAY_AND_CONTENT.md). Asset paths
are now descriptive and manifest-resolved; all original bytes are preserved.
The input tape, UTC clock, seed and JSONL traces provide the reference for
future typed simulation work. CTest also runs replay and asset-provenance checks.
