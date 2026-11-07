# Typed input and camera systems

The default runtime now uses typed C++ input state and native camera calculations.
Movement, weapons and match logic still run through the compatibility runtime.
The checked-in gameplay trace was **not regenerated** for this refactor.

## Ownership

| Module | Responsibility |
|---|---|
| `coordinates.hpp` | Distinct world, stage, pixel and velocity types |
| `input.hpp`, `input.cpp` | Physical key/button state, pressed/released edges, action bindings and `PlayerInput` |
| `platform_input.cpp` | GLFW/raylib key-code adapter and viewport-aware mouse sampling |
| `camera.hpp`, `camera.cpp` | Camera position, tuning, follow/pan/vehicle framing, supplied shake noise, grid snapping and transforms |
| `camera_bridge.cpp` | Original block-ID mapping, legacy global mirrors and compatibility tuning |
| `replay.cpp` | Input recording format and gameplay/system trace serialization |

The input and camera cores have no graphics/window or VM dependency. Their
public values can be used by the next typed movement system. Legacy string key
names and control-list entries are decoded at the compatibility boundary;
replay files retain their existing version-1 format.

## Input flow

`platform sample or replay frame → RawInput → InputSystem → ControlBindings → PlayerInput`

One snapshot supplies held, pressed and released states. Remapping does not
latch an old binding. Wheel bindings are one-tick pulses. The 15 action slots
match the original controls: jump, left, right, crouch, use item, crawl/dive,
reload, chat, map, previous/next inventory slot, primary/secondary/gadget slot,
and exit helicopter.

`PlayerInput` retains all individual action states as well as a horizontal axis,
stage-space aim and world-space aim. Opposing movement buttons therefore remain
available to reproduce the original facing rules in a future movement rewrite.
World aim is sampled using the camera at the start of the tick. The remaining
weapon scripts keep their existing aiming calculations and execution order.

Live sampling and recording share the same viewport transform. Live input can
extend beyond the viewport; recording clamps to the supported stage bounds,
preserving the previous recording behavior. A compatibility-origin marker keeps
the original live/replay wheel-dispatch order. Debug restart handling and dialog
character input remain in their existing app/VM adapters.

## Camera flow

Camera state is owned by `CameraSystem`. The bridge mirrors position, grid origin
and shake magnitude to the globals still read by original scripts. The 26 mapped
camera writes run at their original scheduling points; this avoids moving camera
updates across physics, aiming or broadcast boundaries during the refactor.

The native calculations cover player and aim-led follow, helicopter/passenger
framing, spectator pan, spawn positioning, bounds, grid snapping and shake.
Shake takes an explicit noise sample. The bridge evaluates the original random
reporter so RNG order and source-block trace identities remain unchanged.

World coordinates retain upward-positive Y. World/stage conversion subtracts or
adds the camera; viewport conversion handles centered stage coordinates and
downward-positive pixels. Fractional coordinates are preserved. Camera easing
and grid snapping use `floor(value + 0.5)`, retaining Scratch-style negative-half
rounding. The window remains 1280×720 with a 640×360 logical stage.

### Preserved compatibility quirk

Direct comparisons exposed an existing VM bug: boolean reporter values are
stored as `"true"`/`"false"` strings, and its numeric conversion produces zero.
That suppresses the original expression's fast-fall smoothing adjustment and
low-altitude helicopter offset. Fixing this would change behavior beyond a
system extraction.

The bridge explicitly configures the camera to the observed compatibility
profile (vertical smoothing 10 in both fall cases, low-altitude offset 0).
The standalone camera supports normal typed tuning, including fast-fall
smoothing 4 and offset -60. Correcting VM boolean arithmetic and changing the
compatibility profile should be a separate, measured behavior change.

## Verification

```powershell
.\tools\build.ps1 -Test -Preview
python tools/test_input_camera.py bin/battle_bound_dx.exe
```

Checks cover:

- Key/mouse edges, held input, reset, rebinding, unknown keys and typed actions.
- Fractional coordinate round trips, Y inversion, negative-half rounding,
  fast-fall thresholds, helicopter framing, bounds, grid snapping and supplied noise.
- 104 direct comparisons of all 26 camera writes against original VM expressions,
  including RNG results and trace identities, across four state variants.
- Unchanged 300-tick gameplay fixture plus a 600-tick scenario with changing aim,
  movement, jumping/crawling, inventory selection, wheel input and firing.
- Byte-identical native/legacy gameplay and system traces over all 900 ticks.
- Vulkan/headless gameplay and system trace parity over 300 ticks.
- The existing deterministic, content, asset and single-executable checks.

For diagnostics, add `--systems-trace scratch/systems.jsonl` to a replay command.
It records camera/grid/shake, aim, horizontal input and each action's three button
flags. Action arrays follow the `Action` enum order. `--legacy-camera` selects
the retained original-expression path for comparison; native camera is default.
The original gameplay trace format and golden files remain unchanged.

Tests write traces to temporary directories. System traces use the same 16 MB
per-output limit and reservation checks as gameplay traces. The workspace stays
under the existing 450 MB tooling guard and requested 500 MB ceiling.

Remaining scope: manual interactive/menu/rebinding coverage, full vehicle
sessions and cross-device certification. Expression tests cover vehicle camera
math, not a complete helicopter playthrough. The next extraction can consume
`PlayerInput` and the camera-independent world types for movement/collision.
