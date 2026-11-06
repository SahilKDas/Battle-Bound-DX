# Vulkan build verification — November 5, 2026

Command: `tools/build.ps1 -Test -Preview`, after the user authorized testing.

- Release C++26 build with warnings treated as errors: passed.
- Vertex/fragment SPIR-V validation for Vulkan 1.0: passed.
- PE imports: Windows system DLLs only; Vulkan is loaded dynamically through GLFW.
- Scratch value/procedure/loop/clone/broadcast regression checks: passed.
- Original startup, 300 headless ticks: passed, zero unsupported opcodes.
- Offline gameplay, 300 headless ticks: passed, zero unsupported opcodes.
- Scripted movement/combat, 300 headless ticks: passed; 111 world units of
  horizontal displacement range and 24 bullet clones created.
- Isolated single-EXE test: passed from a temporary folder containing only the
  executable, with no asset root and no development toolchain on PATH.
- Vulkan graphics path: a hidden window rendered 90 gameplay ticks, captured
  the swapchain image through a staging buffer, and exited successfully.
- Visual inspection: `scratch/native-preview.png` is 1280×720 and shows the
  map/scenery, player, held weapon and HUD. The test window was closed normally.

The packaged executable is approximately 27.81 MB. Workspace usage after this
run is approximately 98.41 MB, beneath the 450 MB tooling guard and 500 MB ceiling.

This is a smoke/regression check on this Windows host, not full game parity or
cross-device certification. Manual input, all menu/vehicle/mode branches,
swapchain recreation, validation-layer diagnostics, audio, and real multiplayer
still need separate coverage. The developer executable remains unsigned.

## Replay/content milestone — November 6, 2026

`tools/build.ps1 -Test -Preview` passes all six CTest checks, including the new
deterministic replay and asset-provenance tests. The replay check runs separate
processes, changes timezone settings, tests seed/input/time sensitivity and
rejects malformed input. A 300-tick fixture records 24 bullet clones and a
130-unit horizontal displacement range; its full JSONL trace is 160,574 bytes.

The Vulkan-rendered exercise matches the headless fixture byte-for-byte across
all 300 ticks. Its final captured frame was visually inspected after the named
asset migration. The isolated executable check also passes with no asset tree
or development toolchain on PATH. Test subprocesses have explicit time limits.

All 815 renamed source assets match both their retained SHA-256 checksums and
original Scratch MD5 identities; all 1044 sprite/asset aliases resolve. The
typed catalog validates 26 item IDs and four match modes. Workspace usage after
the milestone is approximately 99.84 MB. Trace/recording fixtures total 188,109
bytes. See `REPLAY_AND_CONTENT.md` for commands, limits and remaining scope.
