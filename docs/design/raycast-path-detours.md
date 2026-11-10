# Raycast path check and detours (todo 48)

Evaluation and design as built, 2026-11-10. Nothing here has run in the game. The feature is built behind a console
switch that is **off by default**, because the open questions below can only be answered in game and the same code is
the instrument that answers them. The designer decides after the checks whether it stays, and whether it is on by
default.

## Problem

Primm, 2026-11-08, with the todo 43 overlay: the car is not in the navmesh and the pathfinder's route runs straight
through it. The Walker only notices once it is physically stuck, and then the recovery ladder reacts. A way round
exists; the Walker does not look for it before walking into the car.

## What was built

1. **Sweep** (engine-free, `src/Movement/PathProbe.*`, host-tested). A line over the ground is checked with rays the
   way the player would walk it: a ray straight down every 20 units finds the floor, and forward rays at knee
   (40 units) and chest (90 units) height run from one floor sample to the one 60 units later, each ray starting one
   sample early so two spans overlap. Following the floor is what keeps a slope, a hump or stairs from reading as a
   wall. A rise of more than 28 units between two floor samples is "too high to step onto"; a fall of more than 60, or
   no floor, is a drop.
2. **Check**. On every route search (0.25 s) the Walker sweeps the next 400 units (5.7 m) of its route, on the centre
   line only, minus the last 100 units before the destination.
3. **Search**. When an obstacle or a rise blocks it, twelve points are tested: 80, 130, 190, 260, 340 and 430 units
   (1.1 m to 6.1 m) to the left and right of the blocked spot, 45 units back from it. A point is taken when (a) the line from the player to
   it is free at the player's width (centre ray plus a ray 22 units to either side), (b) it is on the navmesh, and
   (c) the line from it back onto the route 350 units (5 m) beyond the blocked spot, or 550 (7.9 m), tried second, is
   free at the player's width. Nearest points first. At most 300 rays a frame; a search that needs more carries on next frame.
4. **Detour**. The Walker steers straight at the point (violet in the overlay) instead of the path node. It ends when
   the player is within 45 units of it, when the route is free again from where the player stands (checked with side
   rays 35 units out, so it is not dropped the moment the centre line is free by a hair), when something gets into the
   line to the point, after 8 s, or when the stuck recovery takes over. Then the route is searched again from there.
5. **Side and budget**. A side once taken is kept for later detours on the same walk (other-side points cost 250 units
   more). Without one, the side where only one side ray was blocked is avoided (60 units). A walk may take 6 detours;
   the count and the side reset when the player is 5 m closer to the target than where a detour last started. After a
   search that finds nothing the next one waits 1.5 s.
6. **Nothing found** means nothing changes: the walk goes on as before and the existing stuck recovery deals with it.

Console (both new, registered after `NWalkerDebug`, so they take the next two opcodes):

- `NWalkerDetour` toggles, `NWalkerDetour 1` / `0` set it, `NWalkerDetour 2` switches it on without the navmesh
  requirement. Optional second number: the collision layer the rays are cast as (default 37, line of sight).
- `NWalkerProbe [rays] [layer]` measures from where the player stands, looking where the player looks, and writes to
  `neuro-fnv.log`: the character controller's shape, what a knee, chest and floor ray hit when cast as six different
  layers, what a burst of rays costs (60, 400 and 3500 units long, floor rays, and with the reference lookup), and what
  the sweep makes of the 600 units (8.6 m) ahead. With `NWalkerDebug 1` the sweep's rays are drawn.

Overlay (`NWalkerDebug 1`): grey rays hit nothing, red rays end where they were blocked, a red pole marks the blocked
spot, small frames mark the points tried (grey thrown out, violet taken), and the detour is a violet pole with its two
legs.

Log, at every log level: one line when a detour starts (what blocked the path, how far ahead, which side, how many
rays), one when it ends (why), one per route when no way round was found (how many points failed on which test), and
at the end of each walk the rays cast, the time they took in all and the worst frame.

## The six questions

### 1. What the rays hit

Source-backed:

- The ray is the one line of sight already uses (`TES::Pick`, 0x458440). It is cast "as" a collision layer, which
  decides what it can hit. Line of sight uses layer 37 (LINEOFSIGHT) and that is the default here, because it is the
  only layer this project has seen work in game.
- A hit tells more than the old helper read: the reference behind it (`TES::Pick` returns the `NiAVObject`,
  `TESObjectREFR::FindReferenceFor3D` 0x56F930 maps it to a reference), the collision layer of the thing hit (low 7
  bits of the collidable's filter info), and the surface normal. Sources are named at `WorldUtils::CastRay`.
- So the check can tell actors, doors and the walk's own target from everything else, and it does: an actor never
  blocks (a companion in the way must not reroute the walk), a door blocks without a detour (the stuck recovery opens
  doors), and a hit on the target ends the sweep as clear.
- Rays ignore the player's own collision group, as line of sight does since 2026-11-08.

Not known until measured (`NWalkerProbe` logs each):

- Which objects a LINEOFSIGHT ray passes through that the player does not: glass, chain-link fences (TRANSPARENT),
  loose clutter, invisible walls. Layer 30 (CHARCONTROLLER, the layer of the player's own body) should hit exactly what
  the player collides with, but no source shows a ray cast as that layer. The collision filter's layer table is not in
  any reference, so this is a measurement: compare the lines for LINEOFSIGHT and CHARCONTROLLER at a fence, a window, a
  bush and a pile of clutter.
- Whether the floor ray reports the ground under every kind of floor (terrain, statics, bridges).
- Whether a ray that starts inside a mesh hits its far side. The overlap of the spans exists because of this.
- Whether the normal at 0x30 is a unit vector for every hit. Only itr-nvse reads it. It is logged, not used.
- The byte at 0xAC that itr-nvse treats as "no result". Logged as "budget byte set".

Not handled: an actor in front of a wall hides the wall for that ray (the ray stops at the first hit).

### 2. Cost

In rays, from the host tests:

| Step | Rays |
| ---- | ---- |
| Check of 400 units of route, centre line | 34 (20 floor, 14 forward) |
| The same at the player's width (while a detour runs) | 62, plus the leg to the waypoint |
| One point of a search | about 50 to 250 (two or three legs of 3 to 9 m) |
| A whole search, all twelve points fail on the last test | up to about 2500, over about 8 frames |

So a normal walk costs about 136 rays a second, and a search is a one-off burst capped at 300 rays a frame (plus the
sweep that crosses the cap). What a ray costs in milliseconds is not known: run `NWalkerProbe` and read the
"microseconds each" lines, and read the "Walker path check on the walk to ..." line after a walk for the real total
and the worst frame. If a ray costs 10 microseconds, the check is 0.3 ms every quarter second and a search frame 3 ms.

### 3. Heights and width

- Heights are the designer's knee and chest: 40 and 90 units (0.57 m and 1.29 m). They catch anything solid between
  about 0.6 m and 1.3 m, which should include a car body and a boarded fence. They miss a single rail between the two
  heights and anything lower than 40 units (kerbs, rubble: stepped over or left to the Jump recovery). A third ray at
  hip height costs 7 more rays per check if a fence turns out to slip through.
- Width: the side rays are 22 units from the centre line. That is a guess. JIP-LN-NVSE names the character
  controller's `shapeRadius` (0x5F8) and `shapeHeight` (0x5F4); no second source does, so `NWalkerProbe` only logs
  them. Once the logged radius is confirmed plausible in game it should replace the 22 and the guess should go.
- The route itself is checked on the centre line only. Side rays there would clip every door frame and corner the
  navmesh route turns around.

### 4. Hand-over to the existing logic

- Stuck, slide and oscillation detection keep running during a detour, measured against the waypoint. When one fires,
  the detour is dropped and the recovery ladder runs as before. The recovery budget is untouched.
- The progress stall (70 units in 30 s) keeps counting. A detour is a few metres and lasts at most 8 s, so it should
  not trip it; a walk that only ever detours runs out of detour budget, then of recovery budget.
- The check does not run within reach of the target or during a recovery step (including Wander).
- The last 100 units of the route are not swept, and an obstacle within 60 units of the end of the swept route starts
  no search: that is the target's own surroundings.
- A plain exterior cell crossing keeps the detour and its budget, like the recovery budget.

### 5. Failure cases

- Both sides blocked, a wall across the route, a point off the navmesh: no detour, the walk goes on as before.
- A ledge beside the obstacle: a line that crosses a drop of more than 60 units is not free, so a detour never leads
  off an edge. A drop on the pathfinder's own route is left alone (it may mean to jump down).
- Narrow corridors: the nearest point is 80 units to the side and needs 22 more for the side ray, so in a corridor
  narrower than about 3 m no point passes and nothing changes.
- An obstacle that reaches more than about 6 m to both sides of the route: the furthest point is 430 units out.
- The route after the waypoint is only assumed to be the straight line back onto the old route. The pathfinder can
  only search from where the player stands, so "a path onward" is checked with rays, not with the pathfinder. If the
  new route runs back into the obstacle, the next check starts another detour on the same side.

### 6. Does this remove the need for todo 45?

For what has been seen (one object standing on an otherwise good route): probably yes, if the Primm car check passes.
It does not handle obstacles that need more than one turn (a car against a fence, a U-shaped barricade), areas where
the navmesh itself is missing or wrong, or anything that reaches more than 6 m to both sides. Those are what a grid would add; see
`custom-navigation-grid.md`, which recommends building on this sweep and deciding after the Primm test.

## Numbers that are estimates

All in `PathProbe::Settings`, `PathProbe.cpp` and the constants at the top of `WalkerHandler.cpp`: floor sample every
20 units, forward span 60, knee 40, chest 90, half width 22, rise 28, drop 60, floor ray from 60 above to 120 below,
look-ahead 400, side distances 80 to 430, back-off 45, rejoin 350 / 550, arrival 45, 8 s, budget 6, refill 350,
cooldown 1.5 s, 300 rays a frame, clear-again width factor 1.6.

## Evidence

- `TES::Pick` returns the hit `NiAVObject`: JohnnyGuitarNVSE `JG/internal/Game/Bethesda/TES.cpp` (0x458440);
  JIP-LN-NVSE `internal/jip_core.cpp` (`GetRayCastObject`, `GetRayCastMaterial`).
- `FindReferenceFor3D` 0x56F930, cdecl: JohnnyGuitarNVSE `Bethesda/TESObjectREFR.cpp`; itr-nvse
  `handlers/OnContactHandler.cpp`.
- Ray payload: hit fraction 0x40 and collidable 0x80 in itr-nvse `internal/RayCast.h` and JIP `jip_core.cpp`; normal
  0x30 and the byte at 0xAC in itr-nvse only. `src/itr/internal/RayCast.h` now follows the pinned itr-nvse layout.
- Layer in the collidable's filter info at +0x1C, low 7 bits: itr-nvse `internal/HavokLayout.h`; JIP `internal/havok.h`
  (`hkpWorldObject`: `cdBody` 0x10, `layerType` 0x2C). Layer names: the enum in JIP `internal/havok.h`.
- Revisions read: itr-nvse bb61633, JIP-LN-NVSE 5a30ac4, JohnnyGuitarNVSE 5361798 (all MIT or used for facts only;
  no code copied beyond the already adapted `RayCast.h` fields, covered by `src/itr/itr-license.md`).

## Checks run

`bash tools/test.sh`: 363 cases pass, 25 of them new (one for the overlay drawing; `tests/PathProbeTests.cpp`: floor following on stairs up and
down, kerb, crate, ledge, missing floor, actors, doors, target, narrow gap, thin post, polyline, the search around a
car on both sides and its short side, navmesh rejection, a wall, a walkway with drops, the ray budget, the tracker's
arrival, time-out, budget, refill and cooldown). `clang-format`. `env -u FalloutNVNeuroPlugin bash tools/build.sh
Debug` builds without warnings. The test world is boxes on a flat floor; it proves the logic, not what a Havok ray
does.

## In-game checks

Measurements first, with detours off:

1. Load a save in Primm. `NWalkerDebug 1`, then stand 3 m from the car, face it, and run `NWalkerProbe`. Expected in
   `neuro-fnv.log`: a "character controller" line, 18 "as LAYER ..." lines, five cost lines, two "sweep" lines saying
   blocked after about 3 m by an obstacle, and red rays on the car in the overlay. Send the block of lines.
2. Repeat facing: open ground (expected clear for 600), stairs or a ramp (clear, floor ends above the start), a slope
   up a hill (clear), a kerb, a closed door (blocked by a door), an NPC (clear), a chain-link fence or window (compare
   LINEOFSIGHT with CHARCONTROLLER), a bush, a low fence with rails.
3. From the cost lines: note the microseconds per ray. This is the number todo 45 needs too.

Then the detour:

4. `NWalkerDetour 1` and the Primm walk through the car. Expected: red rays and pole at the car before she reaches
   it, a violet pole to one side, she walks to it and on past the car without touching it; the log has `Walker detour
   1 on the way to ...` and `Walker detour ended: the path is clear again` or `reached the waypoint`.
5. The same walk with `NWalkerDetour 0`: the old behaviour, for comparison.
6. If the log says `no way round was found ... N off the navmesh` at a spot with open ground beside the obstacle, try
   `NWalkerDetour 2`. If that works the navmesh test is the problem, not the idea.
7. A long walk through Goodsprings and Primm with detours on: no detour on stairs, slopes, through doorways or along
   walls. Every false one shows in the log with what it took for an obstacle.
8. A walk whose target is a container, a door and an NPC: it must arrive and interact as before (no detour around the
   target itself).
9. A companion or NPC standing on the route: no detour.
10. A closed door on the route: no detour; the door recovery opens it as before.
11. An obstacle beside a ledge or on a bridge: no detour off the edge.
12. After a few walks, the `Walker path check on the walk to ...` lines: rays, total time and worst frame.
13. If rays cast as LINEOFSIGHT miss things the player bumps into, try `NWalkerDetour 1 30` and repeat 4 and 7.

## Unverified

- Everything under "Not known until measured" above.
- That steering straight at the waypoint works as well as following path nodes (same movement code, different point).
- That the route searched from the waypoint goes on past the obstacle and not back through it.
- `PointOnNavmesh` and `GetPointNavMesh` as the navmesh test for a point in a neighbouring exterior cell.
- Every number in the estimates list.
