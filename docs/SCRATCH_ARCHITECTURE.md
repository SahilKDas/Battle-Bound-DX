# Battle Bound: original project reference

Static analysis of `Battle Bound.sb3`, November 4, 2026. The original archive has
not been modified. This is a source analysis, not a completed playtest or a
claim that every legacy branch runs. No DX engine has been selected yet.

## Browse and reproduce

Run `python tools/unpack_scratch.py` from the repository root (Python standard
library only). Outputs:

- `scratch/original/project.json`: exact Scratch project definition.
- `scratch/original/*`: original hash-named SVG, bitmap, and sound files.
- `scratch/scripts/*.txt`: all top-level stacks, nested control stacks,
  reporter expressions, custom calls, and original block IDs.
- `scratch/inventory.json`: source SHA-256, variables, lists, costumes, sounds,
  comments, custom block metadata, and broadcast senders/receivers.
- `scratch/items.json`: item codes, names/descriptions extracted from the UI
  SVG text, categories, and saved gear costs.
- `scratch/assets.html`: local visual/audio asset catalog; open in a browser.

Listings use raw Scratch opcodes rather than executable Python or JavaScript.
`variable:'name'` means a variable reporter, while `'name'` is a literal.
Disconnected top-level stacks are included and must not be mistaken for active
event handlers. Scratch scheduling, type coercion, missing inputs, and costume
lookup behavior are not simulated by this tool.

## Scale and ownership

The archive contains 816 files. There are 15 targets (stage plus 14 sprites),
15,331 block entries, 979 costume references including stage backdrops, and
65 sound references. References share files, so these are not unique asset counts.
The only declared extension is `pen`.

| Target | Blocks | Responsibility |
|---|---:|---|
| Stage | 126 | Startup broadcasts, backdrop, soundtrack and volume |
| Blank | 1 | Mostly explanatory comments |
| [Player] | 2,651 | Local movement, camera, animations, arms, health, spawn, match rules, teams, votes, scoring, helicopter passenger |
| [Bullet] | 2,076 | Local weapon use, inventory, magazines, projectiles and hit effects |
| [Opponents] | 3,030 | Remote avatars, smoothing, remote weapon effects and damage to local player |
| Cloud sprite (cloud symbol) | 438 | Player serialization, cloud reads/writes, session identity and activity tracking |
| [PLATFORM] | 245 | Collision geometry, scrolling map chunks, doors |
| [BACKGROUND] | 628 | Scenery, parallax, map-specific objects |
| [Particles] | 1,201 | Explosions, sparks, dust, smoke, flames and helicopter effects/state |
| [UI] | 2,500 | Menu, loadout, hats/colors, settings, controls, statistics, save/load |
| [GAME UI] | 1,231 | HUD digits, voting, chat, minimap, team/vehicle actions |
| [DATA] | 300 | Save-string encoding/decoding and restoring settings/data |
| [Detector] | 158 | Runtime resolution/fencing checks, activity/report handling |
| [Introduction] | 633 | Intro, help, asset preloading and transition overlays |
| Graphics | 113 | Title/graphic composition and additional loose setup scripts |

## Startup and game loop

1. Stage green flag handler (`avA`) stores the username, clears pen output,
   waits for `Check screen ratio`, sets `DEV.MODE=0`, then broadcasts
   `Green Flag` and `Introduction`.
2. The introduction builds clones for intro/help/skip. Its first-load path
   broadcasts `Load Assets` **and waits**, then broadcasts `MENU`.
   Asset handlers cycle costumes to preload them.
3. Menu code in `[UI]` configures the session. Starting play waits for `init`
   (received by the cloud sprite) and broadcasts `Start`. Resume/revive has
   separate branches, so a fresh session and a resume are different operations.
4. `[Player]`'s `Start` handler (`awY`) clears session lists/counters, resets
   player state, and enters a forever loop (`T%`). Each iteration runs:
   `Oppdater` → `Spill Kontroll` → spectator, passenger, or physics branch →
   health/chat → layer-order broadcasts → final position update.
5. `Oppdater` (`V_`) broadcasts `update Cloud Engine`, clears pen rendering,
   increments `~Tick` (wraps after 999), resets transient data/arm clones,
   and broadcasts `update` to nine targets.

Both update broadcasts are ordinary broadcasts, **not broadcast-and-wait**.
The listing establishes dispatch order, not completion order across sprites.
Scratch scheduler behavior and custom block `warp` metadata matter when
reproducing the game. Treat this as a shared-state event system, not a clean
single synchronous simulation function.

## State vocabulary and coordinates

Most important state is global on Stage; clone-specific scratch variables live
on individual sprites. Prefixes are conventions, not enforced namespaces.

| Original name | Meaning/use |
|---|---|
| `::Player X`, `::Player Y` | World position |
| `::sx`, `::sy` | Horizontal/vertical velocity |
| `;CAM X`, `;CAM Y` | Camera world position |
| `;SCENE` | Scene string, including `introduction`, `game`, `vote`, and loading/menu transition values |
| `;KART` | Map selection (`kart` = map) |
| `;Spill-Modus` | Game mode |
| `;Ditt Lag` | Your team |
| `::Spiller-liv` | Player health |
| `::Våpen holdt` | Held item code |
| `::[Spiller-action]` | Packed action/status, including spectator/vehicle states |
| `::Høyre/Venstre` | Facing direction |
| `::Krypegå` | Crouch/crawl state |
| `::Spiller-truffet-av` | Last attacker |
| `::VALG` | Vote choice |
| `::Player.INFO` | Mode-dependent data, not a universally typed field |

Drawing generally subtracts camera coordinates from world coordinates. See
`[Player]` `Position` (`Vm`) and `Arm Position`. Map chunks use snapped
`Grid x/y`, a moving 3×3 clone neighborhood, and costume names encoding chunk
coordinates. The chunk setup uses 480×360 multiplied by 150% = 720×540 world
units. This is costume-based collision scenery rather than an explicit tile map.

## Movement, combat and presentation

`Physics` in `[Player]` handles movement/facing and vehicle branches, with
separate X/Y movement helpers. The helpers use `touching [PLATFORM]` and
iterative penetration correction (including a 200-iteration escape guard).
Standing and crouching use different hitbox costumes. Gravity is held in `G`
(saved as -1.2); do not treat every saved variable value as an intended default.

The player sprite separates body animation from arm clones and aiming math.
Movement includes jump, crawl, dive and flip branches. Camera smoothing,
screen shake, world limits and spectator movement also live here.

The project's embedded help describes these intended mechanics: headshots
double damage; crawling reduces explosion damage, hides the minimap marker,
and avoids barbed-wire damage; diving reduces fall damage; flips/dives increase
speed. These descriptions need runtime comparison before exact DX tuning.

`[Bullet]` dispatches `Use Item` by letter code. Projectiles use clone role IDs,
direction vectors, and precision movement/hit routines. Bow charging,
grenade bounce/explosion, melee timers, recoil and magazines have separate
paths. `[Opponents]` contains corresponding remote weapon behavior and local
damage routines, so damage logic cannot be understood from `[Bullet]` alone.
`[Particles]` is also gameplay-relevant: it includes flame damage and vehicles.

There are 25 letter-coded loadout items plus a `heli` entry:

| Codes | Items |
|---|---|
| a–e | Hands, pistol, axe, SMG, bow and arrow |
| f–j | Grenade, shotgun, sniper, pet cube, RPG |
| k–o | Shurikens, knife, shield, machinegun, assault rifle |
| p–t | Heavy pistol, katana, semi-auto rifle, light SMG, taser |
| u–y | Medic gun, medkit, nunchaku, smoke bomb, flamethrower |

Primary/secondary/gadget lists determine loadout categories. Saved inventory
is `d,l,f` (SMG, knife, grenade). The saved maximum gear budget is 950.
Descriptions in `items.json` are original UI claims, not measured combat stats;
derive authoritative damage/rate/reload behavior from the matching scripts.

Saved control bindings are W/A/D/S, mouse, Space, R, T, M, wheel up/down,
1/2/3 and Q. These are configurable list entries, not hardcoded universal keys.

## Match rules

Mode IDs 1–4 map to Team Death Match, Gun Game, Capture the Flag and Free for
All (confirmed by consecutive HUD costumes and selection arithmetic).
Embedded instructions describe four-team TDM, randomized weapon progression
on kills in Gun Game with gear costs disabled, two-team CTF, and ordinary FFA.

`Spill Kontroll` owns team setup, mode processing, voting, match-end info,
capacity priority and map selection. A particularly important legacy behavior:
round/vote phases use the local wall clock's **minute and second**, not simply
elapsed time since joining. With saved `&Spill-lengde=10`, the first 30 seconds
of each ten-minute boundary are voting; the embedded UI describes 9:30 rounds.
That appears intended to align clients without an authoritative match server.
The DX replacement should make that synchronization an explicit decision.

## Cloud networking

This is state exchange through eight cloud variables, not an authoritative
server simulation. `☁ Online` separately carries a version check; cloud sprite
green flag code sets `Version=16` and has an author-username-specific update.

`update Cloud Engine` runs UpdatePlayersList(first pass), Read Cloud,
SaveToCloud(random cloud slot), CountPlayers, UpdatePlayersList(second pass).
`Read Cloud` compares cached slot strings and decodes changed slots. Players
are identified by username plus session `@ID`; Init derives that ID from
days-since-2000 arithmetic modulo 1000. New remote records create opponent clones.
Activity/check-value/offset lists track remote update freshness.

`SavePlayer` (`@0`) defines this **20-field** wire record, in order:

1. Tick
2. Username
3. Rounded world X
4. Rounded world Y
5. Session ID
6. Animation
7. Arm direction
8. Facing
9. Cloud arm direction
10. Action/status
11. Held item
12. Color
13. Last attacker
14. Vote
15. Mode-dependent player info
16. Head cosmetic
17. Chat phrase
18. Color brightness
19. Helicopter ride username
20. Player game points

Encoding maps characters to two-digit costume indices and appends `01` as
a field separator. Decode maps two digits back through numeric costume names;
costume index 1 terminates a field. The cloud sprite costumes are therefore
part of the protocol, not just graphics. `#CloudList` flattens records using
`&MaxElements` as stride. Loose setter stacks set that variable to both 18
and 20, and the saved local player list has 19 entries: use the active
`SavePlayer` implementation as evidence, not the stale snapshot alone.

Remote movement smoothing is implemented in `[Opponents]` (`Glatt posisjon`).
Weapons/actions are reconstructed from remote state; the listed record has
no independent packetized shot event or full authoritative health snapshot.
Exact reconciliation, stale-player timeout and collision/identity edge cases
still need runtime validation.

## Menus, persistence, and DX boundaries

UI uses clones, costumes, stamps and custom text/digit drawing. Loadout,
cosmetics, controls, volume/graphics and statistics are all already represented.
Chat is a predefined phrase list, with the selection sent in player state.
`[DATA]` exposes save/load strings through `encrypt`/`decrypt`, control packing,
colon-separated parsing and a `validkey` sentinel. These names do not establish
cryptographic security. No conventional filesystem/database save backend is
present in this project definition.

For DX, preserve the observable movement/combat rules first, then separate
simulation, rendering, input, content data, networking and persistence. Avoid
translating the existing sprite boundaries directly into engine modules:
player, opponents and particles each mix several responsibilities.

Remaining work before claiming behavioral parity:

- Play the original locally and record timings, input feel, camera and damage.
- Trace item-specific numeric tuning into structured definitions.
- Compare all active map/door/hazard/spawn branches with their visuals.
- Exercise two clients to verify remote shots, inactivity, vehicles and voting.
- Resolve original publication/license provenance and third-party media rights
  in `CREDITS.md` before public DX distribution.
