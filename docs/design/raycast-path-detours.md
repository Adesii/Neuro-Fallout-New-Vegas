# Raycast path check and detours (todo 48)

Evaluation and design as built, 2026-11-10 to 12. **On by default since 2026-11-12**: the designer wants features to
work without being switched on, and a switch to turn them off when something goes wrong. `NWalkerDetour 0` is that
switch. The rays were measured in game (see "Measured in game"); the grid and the widened rerouting have not run yet.

The normal form is: sweep the route ahead; when it is blocked, look for a way round on the navigation grid
(`custom-navigation-grid.md`); when there is none, or she gets stuck anyway, give the obstacle to the pathfinder to
route around. The side-step to a single waypoint described under "What was built" came first and is now a debugging
form (`NWalkerDetour 2`): at the Primm car it put her against the car.

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
   it is free at the player's width (centre ray plus a ray to either side, the player's collision radius out: 20.3
   units), (b) it is on the navmesh, and
   (c) the line from it back onto the route 350 units (5 m) beyond the blocked spot, or 550 (7.9 m), tried second, is
   free at the player's width. Nearest points first. At most 300 rays a frame; a search that needs more carries on next frame.
4. **Detour**. The Walker steers straight at the point (violet in the overlay) instead of the path node. It ends when
   the player is within 45 units of it, when the route is free again from where the player stands (checked with side
   rays 32 units out, so it is not dropped the moment the centre line is free by a hair), when something gets into the
   line to the point, after 8 s, or when the stuck recovery takes over. Then the route is searched again from there.
5. **Side and budget**. A side once taken is kept for later detours on the same walk (other-side points cost 250 units
   more). Without one, the side where only one side ray was blocked is avoided (60 units). A walk may take 6 detours;
   the count and the side reset when the player is 5 m closer to the target than where a detour last started. After a
   search that finds nothing the next one waits 1.5 s.
6. **Nothing found** means nothing changes: the walk goes on as before and the existing stuck recovery deals with it.

Console (both new, registered after `NWalkerDebug`, so they take the next two opcodes):

- `NWalkerDetour 0` switches the path check off, `NWalkerDetour 1` back on in its normal form, `NWalkerDetour` alone
  toggles. For debugging: `2` side-steps to one waypoint instead of using the grid, `3` hands every blocked spot to
  the pathfinder at once, `4` is `2` without the navmesh requirement. Optional second number: the collision layer the
  rays are cast as (default 37, line of sight). The setting lasts until the game is closed.
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

## The Primm car test and rerouting (2026-11-10)

The designer ran the detour at the Primm car. It did not help: the side-step put the player against the car. The car
does not stand on an open route, it fills the only way through at that point. The other ways into Primm are under the
bridge and round, or up and over the bridge, both much longer on the navmesh, so the pathfinder keeps choosing the way
through the car. No point beside the obstacle can fix that; the pathfinder itself has to be sent another way.

The engine has this built in. A path request carries a list of "avoid nodes", and the search multiplies the cost of
the navmesh triangles they cover. The engine uses them to path NPCs around each other. Read from FalloutNV.exe
1.4.0.525 itself (disassembly), and matching JohnnyGuitarNVSE's `PathingAvoidNode.hpp` / `PathingRequest.hpp` and
JIP-LN-NVSE's `avoidNodeArray // 94`:

- The seventh argument of the request builder this project already calls (0x9DBC90, so far always null) is stored as
  the request's avoid node array (`PathingRequest::SetAvoidNodeArray` 0x6D61E0, a reference-counted pointer at
  request+0x94; the request's destructor releases it).
- The engine builds one with its allocator (0xAA13E0, 0x18 bytes), the constructor 0x6E3850 and an append (0x905450)
  that copies 0x24 bytes per node: point, second point, radius, cost, type.
- For a sphere (type 0) the search multiplies a triangle's cost by 1 + cost x (1 - d^2 / r^2), where d is the distance
  from the node to the triangle's centre (0x6DC780, used from the search at 0x6A74C8). The engine's own nodes have
  cost 2.0. Only the triangle's centre is tested, so a sphere that misses the centre does nothing.

What was built on it:

- `Pathing::BuildPath` takes a list of spheres and hands them to the request.
- A blocked spot becomes spheres on navmesh triangle centres, cost 1000: the way through stays possible when it is
  the only one and loses to any other way within a few kilometres. (First two triangles sampled along the route;
  since the first run every triangle under the obstacle, see below.)
- When a spot is given to the pathfinder (`PathProbe::BlockMemory`, host-tested):
  1. the search for a side-step finds nothing;
  2. the route is blocked again within 150 units of a spot that was already side-stepped on this walk;
  3. she gets stuck within 250 units of a spot the path check saw blocked;
  4. she gets stuck for the third time in a row anywhere (then the place right ahead of her is given).
- The spots are kept for later walks in the same interior or worldspace (six at most, the oldest dropped) and
  forgotten on leaving it and on a load. So the second walk past the car takes the other way from the start.
- Once any spot is avoided, the progress stall measures the length of the pathfinder's route instead of the straight
  distance, and starts over when the route changes: the way over the bridge leads away from the target for a while.
- `NWalkerDetour 3` skips the side-step and gives every blocked spot to the pathfinder at once, to test this alone.
- The normal form (`NWalkerDetour 1`, the default) looks for the way round on the navigation grid (todo 45,
  `custom-navigation-grid.md`) and falls back to the pathfinder the same way; `NWalkerDetour 2` side-steps instead.
- Log: `Walker reroute: the pathfinder is to avoid (x, y, z) ...` with the reason, then `the route to X is now N m
  long (was M m)`, or `is still N m long` when nothing changed. Overlay: a red cross and tall pole on each avoided
  centre.

Not known until it runs: whether the search this project calls (`PathManager::Solve`, 0x6D0900, a standard request)
uses the cost function that was read; whether the bridge route is found within the cells that are loaded; whether one
triangle under the car is enough; whether cost 1000 is in a sensible range.

### First run of the rerouting (2026-11-10, `NWalkerDetour 3`)

- The pathfinder reacts. With two spots avoided the route went from 114 m to 119 m and led west around the car
  instead of south through it. So the request builder's seventh argument, the node layout and the cost are right.
- One triangle at a time is not enough. The pathfinder threaded through the next triangle of the same car, the check
  added two more spots, and she ended up on the other side of the car with a route back through it, stuck, and the
  walk failed after eight recovery attempts. Later blocks at the same car were also refused as "the same spot" (within
  150 units), so nothing more was avoided.

Changed in answer:

- A spot is now the whole ground the obstacle stands on: every navmesh triangle that overlaps the bounds of the
  object that blocked (`WorldUtils::ReferenceBound`, up to a radius of 800 units), or a circle of 150 units just past
  the blocked spot when there is no usable object (terrain, a building). Up to 16 triangles a spot, a sphere of
  radius 100 on each centre. `PathProbe::TriangleTouchesCircle` and `Pathing::TriangleCentresTouching`.
- A block on the same object is the same obstacle wherever it is seen from, so it is not avoided twice and not
  side-stepped again.
- The log line is now `Walker reroute: the pathfinder is to avoid N navmesh triangle(s) (M new) under ...`.

Still open: whether the car's bounds cover the gap it fills, and whether the pathfinder then takes the bridge.

## Measured in game (2026-11-10)

The designer ran `NWalkerProbe` four times outside Primm (cells 000E1A0D, 000E1A14, 000E1A15), rays cast as
LINEOFSIGHT for the cost lines:

| Ray | Microseconds each |
| --- | --- |
| 60 units, chest height | 3.7 to 4.0 |
| 400 units, chest height | 4.6 to 5.9 |
| 3500 units, chest height | 5.7 to 9.5 |
| 180 units down | 4.5 to 5.7 |
| 400 units with the reference lookup | 4.6 to 6.0 (the lookup costs nothing measurable) |

- A whole sweep of 600 units took 253 to 260 microseconds on the centre line (50 rays) and 388 to 410 at the player's
  width (90 rays): about 5 microseconds a ray, floor following included.
- The character controller reads radius 2.8932 and height 18.2880 Havok units: 20.3 and exactly 128.0 game units, the
  standard actor height. The JIP-LN-NVSE offsets hold, so the radius is now what the side rays use.
- The reference behind a hit, its layer and the normal all read sensibly: statics with their form IDs, "NCR Trooper"
  as base type 42 on layer CHARCONTROLLER, the landscape with no reference on layer GROUND, normals of unit length
  that match what was hit (0, 0, 1 on a floor, -1, 0, 0 on a riser, tilted on slopes). The byte at 0xAC was never set
  in 40,000 rays.
- A slope: a level knee ray hit the ground after 217 units, and the sweep still called 600 units clear (floor 5 units
  higher at the end). This is the false wall that floor following is there to avoid.
- Stairs: level knee and chest rays hit risers after 147 and 201 units, and the sweep followed the steps 341 units up
  over 600 units and called them clear.
- An NPC 63 units ahead: both level rays hit him; the sweep ignored him and reported the static 387 units behind
  him.
- Rays cast as LINEOFSIGHT, CHARCONTROLLER, PROJECTILE and CAMERAPICK hit the same things at the same distances in
  all four spots. Cast as PATHPICK or BIPED they went through the NPC and hit the static behind him. So the layer a
  ray is cast as does change what it hits, a ray can be cast as CHARCONTROLLER, and PATHPICK is a way to see past
  actors.
- Open from this run: at the first spot the sweep stopped after 240 units at "a rise too high to step onto" on static
  00156881, with the player not on the navmesh (standing on something). Whether that was a real ledge or steps the
  player can walk up is not known; the probe now logs how high the rise was.

Not covered by this run: anything transparent (glass, chain-link), bushes, loose clutter, a door, an interior, and
the detour itself.

## The six questions

### 1. What the rays hit

Source-backed, and confirmed in game where marked:

- The ray is the one line of sight already uses (`TES::Pick`, 0x458440). It is cast "as" a collision layer, which
  decides what it can hit. Line of sight uses layer 37 (LINEOFSIGHT) and that is the default here.
- A hit tells more than the old helper read: the reference behind it (`TES::Pick` returns the `NiAVObject`,
  `TESObjectREFR::FindReferenceFor3D` 0x56F930 maps it to a reference), the collision layer of the thing hit (low 7
  bits of the collidable's filter info), and the surface normal. Sources are named at `WorldUtils::CastRay`. All
  three confirmed in game.
- So the check can tell actors, doors and the walk's own target from everything else, and it does: an actor never
  blocks (a companion in the way must not reroute the walk; confirmed with an NCR trooper), a door blocks without a
  detour (the stuck recovery opens doors), and a hit on the target ends the sweep as clear.
- Rays ignore the player's own collision group, as line of sight does since 2026-11-08.
- The floor ray finds terrain (layer GROUND) and statics. Confirmed for both.

Still not known:

- Which objects a LINEOFSIGHT ray passes through that the player does not: glass, chain-link fences (TRANSPARENT),
  loose clutter, invisible walls. A ray can be cast as CHARCONTROLLER (confirmed), which should hit exactly what the
  player collides with, but the two have not been compared at anything where they could differ.
- What PATHPICK misses besides actors. It is the engine's own layer for path picks and it sees through actors, which
  would end the "actor hides the wall behind" gap below; it may also see through doors or clutter. Compare
  `NWalkerProbe 2000 38` with the default at a closed door before trying `NWalkerDetour 1 38`
  (the normal form with rays cast as PATHPICK).
- Whether a ray that starts inside a mesh hits its far side. The overlap of the spans exists because of this.

Not handled: an actor in front of a wall hides the wall for that ray (the ray stops at the first hit). The sweep
still found the wall behind the trooper, because its later spans start beyond him.

### 2. Cost

In rays, from the host tests, and in time at the measured 5 microseconds a ray:

| Step | Rays | Time |
| ---- | ---- | ---- |
| Check of 400 units of route, centre line | 34 (20 floor, 14 forward) | 0.17 ms |
| The same at the player's width (while a detour runs) | 62, plus the leg to the waypoint | about 0.5 ms |
| One point of a search | about 50 to 250 (two or three legs of 3 to 9 m) | 0.25 to 1.25 ms |
| A whole search, all twelve points fail on the last test | up to about 2500, over about 8 frames | about 13 ms in all, 1.5 ms a frame |

A normal walk costs about 136 rays a second, 0.7 ms of every second. A search is a one-off burst capped at 300 rays a
frame (plus the sweep that crosses the cap). Cost is not a concern at these numbers. The "Walker path check on the
walk to ..." line after each walk gives the real total and the worst frame. An interior has not been measured.

### 3. Heights and width

- Heights are the designer's knee and chest: 40 and 90 units (0.57 m and 1.29 m) on a body 128 units tall. They catch
  anything solid between about 0.6 m and 1.3 m, which should include a car body and a boarded fence. They miss a
  single rail between the two heights and anything lower than 40 units (kerbs, rubble: stepped over or left to the
  Jump recovery). A third ray at hip height costs 7 more rays per check if a fence turns out to slip through.
- Width: the side rays are the player's collision radius from the centre line, read from the character controller
  (`WorldUtils::PlayerCollisionShape`): 20.3 units in game. The earlier guess of 22 is gone. If the shape cannot be
  read, the check does not run on that walk and the log says so once.
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

- Both sides blocked, a wall across the route, a point off the navmesh: no side-step; the spot goes to the pathfinder
  to route around (see "The Primm car test and rerouting"). If the pathfinder has no other way, the walk goes on as
  before.
- A ledge beside the obstacle: a line that crosses a drop of more than 60 units is not free, so a detour never leads
  off an edge. A drop on the pathfinder's own route is left alone (it may mean to jump down).
- Narrow corridors: the nearest point is 80 units to the side and needs 20 more for the side ray, so in a corridor
  narrower than about 3 m no point passes and nothing changes.
- An obstacle that reaches more than about 6 m to both sides of the route: the furthest point is 430 units out.
- The route after the waypoint is only assumed to be the straight line back onto the old route. The pathfinder can
  only search from where the player stands, so "a path onward" is checked with rays, not with the pathfinder. If the
  new route runs back into the obstacle, the next check starts another detour on the same side.

### 6. Does this remove the need for todo 45?

The Primm car turned out not to be a local problem (see above), and a grid around the obstacle would not have solved
it either: the way round is over a bridge far off. Rerouting through the pathfinder's avoid nodes is aimed at exactly
that and also covers the cases a side-step cannot (a car against a fence, a U-shaped barricade), as long as the
navmesh has another way. What neither does: areas where the navmesh itself is missing. See
`custom-navigation-grid.md`.

## Numbers that are estimates

All in `PathProbe::Settings`, `PathProbe.cpp` and the constants at the top of `WalkerHandler.cpp`: floor sample every
20 units, forward span 60, knee 40, chest 90, rise 28, drop 60, floor ray from 60 above to 120 below,
look-ahead 400, side distances 80 to 430, back-off 45, rejoin 350 / 550, arrival 45, 8 s, budget 6, refill 350,
cooldown 1.5 s, 300 rays a frame, clear-again width factor 1.6. The half width is not one of them: it is the
measured collision radius.

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

Measurements (first run done 2026-11-10; what is left):

1. Primm, `NWalkerDebug 1`, stand 3 m from the car, face it, `NWalkerProbe`. Expected: the two "sweep" lines say
   blocked after about 3 m by an obstacle, and red rays end on the car in the overlay.
2. Wherever the sweep says "a rise too high to step onto": is it something she really cannot walk up? The line now
   gives the height of the rise (limit 28 units per 20). If she can walk up it, send the line: the limit is too low.
3. Facing a closed door (expected: blocked by a door), a chain-link fence or a window, a bush, a low fence with
   rails, loose clutter. Compare the LINEOFSIGHT, CHARCONTROLLER and PATHPICK lines.
4. Once inside a building, for the cost in an interior.

The rerouting (the Primm car), with `NWalkerDebug 1 1` so the route lines are in the log:

- No command needed. The walk to the waypoint behind the car. Expected: a grid search, then `Walker reroute: the
  pathfinder is to avoid ...` (a red cross on the ground at the car), `the route to ... is now N m long (was M m)`,
  and she turns and goes by the bridge or under it. The walk must not end with "never got meaningfully closer" on
  the way.
- `NWalkerDetour 3`, same walk from a fresh load: the reroute line appears about 5 m before the car, no side-step.
- If the log says `is still N m long`: the pathfinder did not react. Send the reroute lines and the `Walker debug
  route` lines around them.
- A second walk to the same place without loading: the route avoids the car from the start.
- After loading the save: the first walk goes to the car again (the spots are forgotten).
- A route that is blocked with no other way (a closed gate with one entrance): she must still walk up to it as
  before, not stand still.

Then the detour:

5. `NWalkerDetour 2` (the side-step form) and the Primm walk through the car. Expected: red rays and pole at the car before she reaches
   it, a violet pole to one side, she walks to it and on past the car without touching it; the log has `Walker detour
   1 on the way to ...` and `Walker detour ended: the path is clear again` or `reached the waypoint`.
6. The same walk with `NWalkerDetour 0`: the old behaviour, for comparison.
7. If the log says `no way round was found ... N off the navmesh` at a spot with open ground beside the obstacle, try
   `NWalkerDetour 2`. If that works the navmesh test is the problem, not the idea.
8. A long walk through Goodsprings and Primm with detours on: no detour on stairs, slopes, through doorways or along
   walls. Every false one shows in the log with what it took for an obstacle.
9. A walk whose target is a container, a door and an NPC: it must arrive and interact as before (no detour around the
   target itself).
10. A companion or NPC standing on the route: no detour.
11. A closed door on the route: no detour; the door recovery opens it as before.
12. An obstacle beside a ledge or on a bridge: no detour off the edge.
13. After a few walks, the `Walker path check on the walk to ...` lines: rays, total time and worst frame.
14. If rays cast as LINEOFSIGHT miss things the player bumps into, try `NWalkerDetour 1 30` and repeat 8.
15. Anything that goes wrong on a normal walk: `NWalkerDetour 0` and try the same walk again. If it works then, the
    path check is the cause; send the `Walker detour` / `Walker reroute` lines.

## Unverified

- That the bounds of the blocking object cover the navmesh triangles of the gap it fills, that the pathfinder then
  takes the other way, and the numbers (bounds up to 800, circle 150, sphere 100 / 350, cost 1000, 16 triangles).
- Everything under "Still not known" above.
- The rise limit of 28 units per 20 (see the open point of the measured run).
- That steering straight at the waypoint works as well as following path nodes (same movement code, different point).
- That the route searched from the waypoint goes on past the obstacle and not back through it.
- `PointOnNavmesh` and `GetPointNavMesh` as the navmesh test for a point in a neighbouring exterior cell.
- Every number in the estimates list.
