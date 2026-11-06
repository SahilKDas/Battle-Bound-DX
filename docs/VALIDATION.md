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
