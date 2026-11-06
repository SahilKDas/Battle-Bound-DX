# Turning the Scratch logic into maintainable C++26

## Recommendation

Use a hybrid migration: keep the compatibility runtime as a behavioral reference,
generate declarative content, and replace one gameplay system at a time with
small, typed C++ systems. Generating one giant function per sprite would carry
the original global coupling and scheduling assumptions into the new code.

| Approach | Benefit | Cost | Use here |
|---|---|---|---|
| Direct block-to-C++ generation | Broad mechanical coverage, source mapping | Dynamic values, shared globals and clone roles remain tangled | A temporary reference backend if bytecode dispatch becomes a bottleneck |
| Manual rewrite | Best ownership, naming and testability | Easy to subtly alter movement/combat before measuring them | Gameplay systems, incrementally |
| Hybrid | Preserves a working baseline while improving architecture | Two implementations coexist during migration | Recommended |

## First preserve behavior

Capture input and per-tick traces from the original and compatibility runtime:
position, velocity, hitbox, animation, health, ammo, fired shots, damage events,
team/mode state and RNG decisions. Inject the clock and RNG; otherwise matches
can change while a test is running because the source uses local wall time.
Use these traces as regression fixtures for each migrated system.

Keep the 30 Hz simulation and existing per-tick units initially. Rendering can
run faster with interpolation. Moving to 60 Hz should be an explicit gameplay
change after parity, with speeds/accelerations/timers converted consistently.

## Replace state with types

Use explicit ownership instead of a renamed global variable dictionary:

```cpp
struct PlayerState {
    Vec2 position;
    Vec2 velocity;
    Health health;
    Facing facing;
    MovementMode movementMode;
    WeaponId equippedWeapon;
    Inventory inventory;
};

struct PlayerInput {
    float horizontal;
    bool jumpPressed;
    bool crouchHeld;
    bool fireHeld;
    bool reloadPressed;
    Vec2 aimWorldPosition;
};

void updatePlayer(PlayerState& player,
                  const PlayerInput& input,
                  const CollisionWorld& world,
                  TickContext& tick);
```

These are proposed interfaces, not compiled implementation claims. Keep world
coordinates independent of window resolution. Keep the original upward-positive
Y convention for simulation and invert Y only at the rendering boundary.
Use fractional positions throughout; perform pixel rounding only when a measured
legacy collision/physics rule explicitly requires it.

Translate `::Spiller-liv` to `health`, `::Våpen holdt` to `equippedWeapon`,
`::Høyre/Venstre` to `facing`, and `;Ditt Lag` to `team`. The original
`::Player.INFO` should become distinct mode-specific fields, not a single
ambiguous member. Replace letter item IDs with `enum class WeaponId`, retaining
an explicit import table for historical data.

## Extract real systems, not sprite classes

Suggested order:

1. **Content:** generated weapon definitions, costs, loadout categories,
   animation frames, cosmetics, sounds and map metadata. Preserve asset IDs and
   provenance. Content generation should produce data, not opaque code strings.
2. **Input/camera:** translate configurable controls into `PlayerInput`; make
   the camera a renderer concern with a stable screen/world transform.
3. **Collision/movement:** port the existing axis-resolution and hitbox rules
   with trace comparisons for jumping, crawling, diving, fall damage and doors.
4. **Weapons/damage:** separate fire eligibility, shot creation, projectile
   movement, impact resolution and visual/audio events. Unify the duplicated
   local/opponent damage rules behind an explicit attacker/target model.
5. **Matches:** mode enums and explicit phase transitions for voting, play,
   results, respawn and spectating. Introduce a supplied match clock rather than
   reading local calendar time throughout gameplay.
6. **Presentation:** scene nodes/draw commands, typed HUD view models and
   ordinary text layouts. Replace digit-costume clones and stamped menu labels.
7. **Vehicles and networking:** model driver/passenger ownership explicitly;
   introduce transport-independent snapshots/events before choosing a server.
8. **Persistence:** versioned settings/loadout/statistics structures with
   validation and legacy save-string import at a single boundary.

Do not move gameplay damage into a particle renderer: the original flame and
vehicle code currently mixes those responsibilities. Likewise, an opponent
should reuse simulation rules and expose replicated state; it should not own a
second copy of every weapon implementation.

## Make event order explicit

The source's ordinary broadcasts are asynchronous dispatches, while
broadcast-and-wait introduces a dependency. Flattening both into direct function
calls would change behavior. Replace each dependency deliberately with phases:

`sample input → movement → weapon intent → projectiles/collisions → damage →
match state → collect presentation events → render`

Choose the final phase order using original traces. Keep an event queue for
`ShotFired`, `DamageApplied`, `PlayerDied`, `Respawned` and `MatchPhaseChanged`.
Use lightweight coroutines only for real sequential flows such as intro fades
or timed dialogs; ordinary physics should be plain update functions.

## Formatting and source traceability

The checked-in `.clang-format` specifies four-space indentation, a 100-column
limit, and expanded control flow. Apply it to new handwritten/generated C++.
Use descriptive names, small functions, headers with narrow public interfaces,
and warnings-as-errors. The existing compressed compatibility implementation
is transitional; it is not the desired formatting or architecture for DX.

Keep an import map linking each migrated routine to its original sprite,
custom block, and block IDs. Review one subsystem's behavioral changes at a time.
Retire the corresponding VM path only after its traces and tests pass.

## Packaging stays independent

Changing simulation architecture should not change deployment: statically link
native dependencies and keep a versioned, read-only content package inside the
Windows executable. No content extraction or writable installation folder is
needed for gameplay. Future saves should go in the user's local application
data directory rather than beside the executable. Preserve credits/licenses
and update them when assets or dependencies change.

Keep build trees in the system temporary directory, retain one final executable,
and enforce the existing 450 MB workspace guard. Do not vendor a toolchain or
duplicate the asset tree per build configuration.
