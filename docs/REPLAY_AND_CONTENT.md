# Repeatable gameplay and typed content

This milestone adds a deterministic reference harness around the compatibility
runtime. It does not yet replace movement or combat with new typed simulation.

## Replay an input recording and compare traces

From the repository root:

```powershell
.\tools\build.ps1 -Test -Preview
.\bin\battle_bound_dx.exe --replay tests/fixtures/gameplay.replay --trace scratch/current.jsonl
python tools/compare_traces.py tests/fixtures/gameplay.trace.jsonl scratch/current.jsonl
```

The executable uses the Windows GUI subsystem. A calling script should wait for
it to exit before comparing its output; the Python test tools do this directly.
For interactive recording, launch with:

```powershell
.\bin\battle_bound_dx.exe --record scratch/my-game.replay --trace scratch/my-game.jsonl --ticks 300
```

This records ten seconds of gameplay and exits. Use `--ticks` to change duration
(1–10000 simulation ticks). Close the window early to save the frames captured
so far. Default mouse/keyboard controls are retained. Recording begins at the
offline gameplay entry point, not the introduction. F5/F6 debug restarts are
disabled during deterministic sessions because they are not replay input events.

For an automated recording:

```powershell
.\bin\battle_bound_dx.exe --headless --exercise --seed 3005 --epoch 1793966460 --record scratch/example.replay --trace scratch/example.jsonl
```

Replay tapes own their seed, UTC epoch, username and complete input sequence.
Clock/seed command-line overrides are rejected when replaying a file. Recordings
are versioned text, bound to the original project JSON's SHA-256. Keys, fire,
logical-stage aim coordinates and wheel input are recorded on every tick.
Default controls, arrows, Enter and printable keys other than semicolon are
supported. Dialog text input, menu/intro workflows and arbitrary debug commands
are outside this gameplay recording format.

## What makes it repeatable

- The simulation remains 30 Hz. Deterministic calendar time is the recorded UTC
  epoch plus simulation elapsed time, independent of the host clock/timezone.
- Scratch `current` and `days since 2000` both use that injected clock, including
  the original session-ID calculation and voting logic.
- A seeded `mt19937` uses an explicitly defined 53-bit mapping for random values;
  standard-library distribution differences are avoided in deterministic mode.
- Restarting a deterministic runtime resets its RNG and diagnostic counters.
- The same sampled input drives recording, replay and scripted exercises.

The reference epoch is **November 6, 2026, 12:01:00 UTC**. Starting at 12:00:00
instead enters the original game's voting phase; a separate regression checks
that distinction. Ordinary unrecorded play continues to use the live clock.

The JSONL trace has a metadata header and one row after each gameplay tick.
Rows include input, simulation time, world position/velocity, health, ammo,
animation, action, held item and its typed name, scene/mode/team/points, actor/task
counts, cumulative shots and RNG draws. Events identify random values, bullet
creation and selected state changes using their original sprite/block IDs.
State values remain strings where Scratch can use mixed numeric/text values.

`compare_traces.py` reports the first differing tick and JSON field. The checked-in
300-tick fixture includes the input, full reference trace and SHA-256 hashes.
It is a compatibility-runtime baseline, not proof of parity with the original
Scratch VM. Floating-point cross-compiler/platform equivalence also remains to
be established; current byte-for-byte checks use this Windows build/toolchain.

To deliberately refresh the reference after reviewing a behavior change:

```powershell
python tools/test_replay.py bin/battle_bound_dx.exe --update-baseline
```

Normal tests never rewrite the reference. They check fresh-process/timezone
repeatability, seed/input/time sensitivity, first-divergence reporting and invalid
input rejection. `-Preview` additionally compares a 300-tick Vulkan-rendered run
against the headless trace and captures the final frame.

## Typed definitions

`src/content.hpp` defines `ItemId`, `ItemCategory`, `ItemDefinition`, `MatchMode`
and `MatchDefinition`. `tools/generate_content.py` generates the readable
`src/generated_content.hpp` from original data, with source references for every
entry. `src/content.cpp` provides checked lookups and validates IDs and values.

The catalog covers all 26 entries (25 loadout items and the helicopter) and four
match modes. Categories, gear costs and reload seconds come from explicit source
lists; names/icons come from original UI assets. Missing values use `std::optional`
rather than a fabricated zero. Unknown codes do not silently select another item.
Damage, magazine capacity and fire-rate branches still need careful extraction;
they are not guessed from UI descriptions or unrelated projectile tables.

The trace harness consumes these definitions for item identity. The compatibility
gameplay still reads its original lists; future rewritten systems will consume
the typed catalog directly. This keeps the first migration step reviewable.

## Descriptive asset names

Actual files now live in `assets/source/<sprite>/<costumes-or-sounds>/<name>`.
Examples include `menus/costumes/smg-icon.svg` and `network/costumes/backtick.svg`.
SVG-derived runtime images mirror those names under `assets/raster/`. Shared
assets have one canonical file, with every original sprite/name alias retained
in `assets/manifest.json`.

The original `scratch/original/project.json` is unchanged. Its hash references
are original identities, resolved through the manifest rather than used as paths.
`verify_assets.py` checks every file's SHA-256 and original Scratch MD5 identity,
all 1044 aliases, and all runtime paths. Searchable script listings now have
names such as `player.txt`, `weapons.txt`, `network.txt`, `menus.txt` and `hud.txt`.
The asset browser, compiler, rasterizer and standalone package all use the mapping.

## Storage guard

The 450 MB tooling guard remains below the requested 500 MB ceiling. Migration
moves assets instead of duplicating them. Regression traces are generated in
temporary directories and removed after tests. Trace files are limited to 16 MB,
recordings to 2 MB/10000 frames, with output reservations checked before writing
inside the selected workspace. Do not accumulate unlimited manual recordings;
overwrite a known output or use the temporary directory for experiments.
