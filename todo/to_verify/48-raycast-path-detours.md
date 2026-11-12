# 48 - Check the path ahead with raycasts and detour around what blocks it (evaluation)

Status: to verify
Area: Walker / pathing (`src/WalkerHandler.cpp`, `src/Movement/`, `src/utils/WorldUtils.*` rays)
Source: designer, 2026-11-08, from the todo 43 debug overlay in Primm
Related: `43-walker-debug-path-overlay.md` (where this was found; the probes are drawn there),
`../45-custom-navigation-grid.md` (the heavier alternative), `25-walker-stuck-recovery.md` (Wander recovery)

## Problem
With the debug overlay on, the designer saw why the Walker runs into the car in Primm: the car is not part of the
navmesh, and the pathfinder's route runs straight through it. So it is not a navmesh gap or a pathing bug, but an
object in the way that the navmesh does not know about. The Walker only notices once it is physically stuck, and then
the recovery ladder (sidestep, back off, Wander) reacts. Exploring a bit does give another route around, so a route
exists; the Walker just does not look for it before walking into the obstacle.

## Idea (designer)
A navigation step on top of the navmesh path: cast rays along the path ahead. When something physical blocks a
segment, put an intermediate waypoint beside the obstacle and path through it, so the route goes around the object
instead of through it. This would make the Wander recovery proactive and is much lighter than the full grid of todo 45.

## Sketch (to be checked in the evaluation)
1. Probe: every route refresh (0.25 s), or less often, cast rays along the next segments of the path, up to a few
   metres ahead. Use the existing ray (`WorldUtils` / `itr/internal/RayCast.h`, which already skips the player's own
   collision group). Cast at knee and chest height, and two parallel rays offset by the player's half width, so a gap
   narrower than the player counts as blocked.
2. Detour: when a segment is blocked, generate candidate points to either side of the hit, perpendicular to the
   segment and a bit past the obstacle. Keep the ones that are on the navmesh (`Pathing::GetClosestNavMeshTriangle`,
   as `PickDetour` does), have a clear ray from the player, and have a path onward to the target. Choose by total
   length and keep the side once chosen (hysteresis), so the route does not flip between left and right.
3. Route: walk to the intermediate waypoint (the Wander step already steers at its own spot through `BuildPath`), then
   continue to the target. Drop the waypoint once passed or once the original segment is clear again.
4. Overlay: draw the probe rays, the hit points and the candidate points with the todo 43 debug overlay, and add the
   probe and detour decisions to its route log (log level 1).

## Questions to answer in the evaluation
1. What the rays hit: the car, statics, clutter, actors (ignore them or not: a companion standing in the way should
   not reroute the whole walk), doors (open them, do not route around them), and ground (a ray along a slope or over a
   small step must not count as blocked).
2. Cost: rays per refresh for a few segments ahead, measured against frame time.
3. Heights and width: the player's collision capsule size, and which heights catch a car's body and a low fence.
4. Hand-over to the existing logic: stuck detection, the recovery budget, the progress stall (a detour makes the path
   longer for a moment), and the interaction range near the target.
5. Failure cases: both sides blocked (fall back to the normal recovery), an obstacle at the target itself, narrow
   corridors where the side points fall off the navmesh.
6. Whether this removes the need for todo 45 in the spots seen so far.

## Work (after the evaluation)
1. Engine-free core in `src/Movement/` with host tests: which segments to probe, candidate generation and scoring, side
   hysteresis, and when to drop the waypoint.
2. Engine side in `WalkerHandler.cpp`: the rays, the navmesh checks and the intermediate waypoint, behind a switch so
   it can be compared with the plain path at the Primm car.

## Done when
A short written evaluation (what the rays catch, cost, chosen design) is in `docs/design/raycast-path-detours.md`, the
designer has decided whether to build it, and if built, the Walker gets around the Primm car without getting stuck.

## Result (2026-11-10)

Evaluation and design: `docs/design/raycast-path-detours.md` (the six questions, evidence, every estimated number).
The questions about what rays hit and what they cost can only be answered in the game, so the feature was built as
the instrument for them, **behind a console switch that is off by default**. The designer has not yet decided whether
it stays or becomes the default; that is what the checks below are for. (The ray measurements were run the same
day: see the playtest section at the end.)

Built:
- **Engine-free core** (`src/Movement/PathProbe.*`, `tests/PathProbeTests.cpp`, 24 cases): the ground-following
  sweep (floor ray every 20 units, forward rays at knee and chest height, rise, drop, doors, actors and the target
  told apart), the points to either side of a blocked spot and their order, the search with a per-frame ray budget,
  and the tracker (arrival, time-out, side kept, budget of 6 that only real progress refills, cooldown after a failed
  search).
- **Rays** (`WorldUtils::CastRay`): the existing Havok ray, now also returning the reference behind the hit, its
  collision layer and the surface normal. `src/itr/internal/RayCast.h` follows the pinned itr-nvse layout.
- **Walker** (`src/WalkerHandler.cpp`): keeps the pathfinder's nodes of each route search, sweeps the next 400 units
  on every search, searches for a way round when blocked, and steers at the waypoint until it is reached or the route
  is free again. With no way round nothing changes. Stuck recovery ends a detour and works as before.
- **Console** (`src/Debug/WalkerProbe.cpp`): `NWalkerDetour [0/1/2] [layer]` (the switch; 2 drops the navmesh
  requirement; the layer the rays are cast as) and `NWalkerProbe [rays] [layer]` (measures ray cost and logs what
  rays hit ahead of the player). They take the two opcodes after `NWalkerDebug`.
- **Overlay**: grey and red probe rays, a red pole at the blocked spot, the points tried, a violet detour.
- **Log**: a line per detour start and end, one per route when no way round was found, and per walk the rays cast,
  total time and worst frame.

Differences from the sketch:
- The route is checked on its centre line only; the side rays (22 units out, plus the centre) are used for the legs
  the search invents and for deciding that the route is free again. Side rays on the navmesh route itself would clip
  door frames and corners.
- The rays follow the floor instead of running node to node, because path nodes are far apart and a straight ray
  between two of them cuts through any slope or stairs in between.
- "A path onward to the target" is checked with rays back onto the old route, not with the pathfinder: `BuildPath`
  only searches from where the player stands.
- The waypoint is steered at directly, not through `BuildPath` as the Wander step does: the line to it was just
  checked with rays.
- Doors are not opened ahead of time; a door on the route starts no detour and the stuck recovery opens it as before.

Checks run: `bash tools/test.sh` (363 pass), `clang-format`, `env -u FalloutNVNeuroPlugin bash tools/build.sh Debug`
(builds, no warnings). Nothing was run in the game; the built DLL is not deployed.

Unverified (needs the game): what a ray cast as LINEOFSIGHT hits and misses (fences, glass, clutter, bushes), whether
a ray can be cast as CHARCONTROLLER, whether the floor ray finds every floor, the hit normal and the reference lookup
on real hits, the cost of a ray, the navmesh test for the waypoint, every number in the design note's estimates list,
and whether the route searched from the waypoint goes on past the obstacle.

### Manual in-game checklist
Measurements first, detours off:
1. Primm, `NWalkerDebug 1`, stand 3 m from the car facing it, `NWalkerProbe`. Expected in `neuro-fnv.log`: a
   "character controller" line, 18 "as LAYER" lines, five cost lines, two "sweep" lines saying blocked after about
   3 m by an obstacle; red rays on the car in the overlay. Send those lines.
2. The same facing open ground, stairs or a ramp, a hillside, a kerb, a closed door, an NPC, a chain-link fence or a
   window, a bush, a fence with rails. Expected: clear on open ground, stairs and slopes; "a door" at the door; clear
   at the NPC.
3. Note the "microseconds each" numbers (also needed for todo 45).

The detour:
4. `NWalkerDetour 1`, then the Primm walk that ran into the car. Expected: red rays and pole at the car before she
   reaches it, a violet pole beside it, she walks round without touching the car. Log: `Walker detour 1 on the way to
   ...` and `Walker detour ended: ...`.
5. The same walk with `NWalkerDetour 0` for comparison.
6. `no way round was found ... N off the navmesh` with open ground beside the obstacle: try `NWalkerDetour 2`.
7. A long walk through Goodsprings and Primm with detours on: no detours on stairs, slopes, in doorways or along
   walls. Each false one is in the log with what it took for an obstacle.
8. Targets that are a container, a door and an NPC: she arrives and interacts as before.
9. An NPC or companion on the route: no detour.
10. A closed door on the route: no detour; the door recovery opens it.
11. An obstacle next to a ledge or on a bridge: no detour off the edge.
12. The `Walker path check on the walk to ...` lines after a few walks: rays, total time, worst frame.
13. If LINEOFSIGHT rays miss things she bumps into: `NWalkerDetour 1 30` and repeat 4 and 7.

Decisions for the designer after the checks: keep it or not; on by default or not; which layer; whether one waypoint
is enough or todo 45's stage 2 (a small on-demand grid) is wanted.

## Playtest 2026-11-10: ray measurements

The designer ran `NWalkerProbe` at four spots outside Primm (slope, stairs, an NPC, a raised static). Detours were not
switched on in this run. Full numbers are in the design note under "Measured in game".

Confirmed:
- A ray costs about 5 microseconds (3.7 to 6 for the short ones); the check is 0.17 ms every quarter second and a
  search frame 1.5 ms. Finding the reference behind a hit costs nothing measurable.
- The reference, the collision layer and the normal of a hit read correctly (statics by form ID, "NCR Trooper" on
  layer CHARCONTROLLER, the landscape on layer GROUND without a reference).
- The sweep followed a slope and a flight of stairs (341 units up over 600) as clear, where a level ray hit them.
- An NPC in the way was ignored and the static behind him found.
- The character controller reads radius 20.3 and height 128.0 units.
- Rays can be cast as other layers. CHARCONTROLLER, PROJECTILE and CAMERAPICK gave the same hits as LINEOFSIGHT;
  PATHPICK and BIPED went through the NPC.

Changed because of it:
- The side rays now use the measured collision radius (`WorldUtils::PlayerCollisionShape`); the guessed 22 units is
  gone. If the shape cannot be read the check does not run on that walk (one log line).
- A blocking rise or drop is logged with its height, in `NWalkerProbe` and in the detour line.

Open from this run: at the first spot (player at -62535, -52027, 6016, not on the navmesh) the sweep stopped after 240
units at "a rise too high to step onto" on static 00156881. Ask the designer what stood there: a real ledge, or steps
or a kerb she can walk up (then the 28-unit limit is too low).

Still to run: checks 1 (the car), 2 (door, fence or window, bush, railed fence, clutter; also compare the PATHPICK
lines), an interior for the cost, and all of 4 to 13 (the detour itself).

## Playtest 2026-11-10: the Primm car, and rerouting

Designer's result: the detour did not help at the car. The side-step put the player against it (the first probe of
the measurement run was taken there). The car fills the only way through at that point; the other ways into Primm are
under the bridge and round, or over the bridge, both much longer on the navmesh, so the pathfinder keeps routing
through the car. A side-step works for an object one can walk around, not for this.

Built in answer (details and the evidence from the game executable in the design note):
- **Rerouting through the pathfinder.** The engine's path requests take "avoid nodes" that multiply the cost of the
  navmesh triangles they cover. `Pathing::BuildPath` now passes them. A blocked spot the Walker cannot pass becomes
  two spheres on the centres of the navmesh triangles under the obstacle, cost 1000.
- **When:** the search for a side-step finds nothing; the route is blocked again at a spot already side-stepped; she
  gets stuck near a spot the check saw blocked; or she gets stuck for the third time in a row anywhere (then the place
  right ahead of her). `PathProbe::BlockMemory`, two new host tests.
- **Kept** for later walks in the same interior or worldspace (six spots at most), forgotten on leaving it or on a
  load.
- **Progress stall:** once a spot is avoided it measures the length of the pathfinder's route, not the straight
  distance, and starts over when the route changes, so the long way round does not fail with "never got closer".
- `NWalkerDetour 3`: no side-step, every blocked spot goes to the pathfinder at once.
- Overlay: a red cross and tall pole on each avoided centre. Log: `Walker reroute: ...` lines.

Choices made without asking (say if any is wrong): the spots outlive the walk; getting stuck three times in a row
anywhere also counts (only while `NWalkerDetour` is on); the progress stall changes its measure as described.

Checks run: `bash tools/test.sh` (365 pass), `clang-format`, the Debug DLL builds (only the old warnings in
`NeuroSDK.cpp`). Not run in the game.

Unverified: that the search this project calls honours the avoid nodes the way the disassembled cost function
suggests; that the bridge route lies within what the pathfinder can search from there; the radius (150 / 350), the
cost (1000) and the two sample distances (40, 120).

### In-game checks for the rerouting
With `NWalkerDebug 1 1`:
1. `NWalkerDetour 1`, walk to the waypoint behind the car. Expected: one side-step, then `Walker reroute: the
   pathfinder is to avoid ...`, a red cross at the car, `the route to ... is now N m long (was M m)`, and she goes by
   the bridge. It must not end with "never got meaningfully closer" on the way.
2. From a fresh load, `NWalkerDetour 3`, same walk: the reroute about 5 m before the car, no side-step.
3. `is still N m long` in the log means the pathfinder did not react: send the reroute lines and the `Walker debug
   route` lines around them.
4. A second walk to the same place without loading: avoids the car from the start.
5. After loading: the first walk goes up to the car again.
6. A blocked route with no other way: she still walks up to it as before.

## Playtest 2026-11-10: first run of the rerouting

Designer's log (`NWalkerDetour 3` at the Primm car): the pathfinder reacted (route 114 m -> 119 m, west around the
car), but only two triangles were avoided, it threaded through the next one, and she ended on the other side of the
car with a route back through it. Eight recovery attempts, then "got stuck". The path check cost 599 rays in 79
frames, 4.8 ms in all, 195 microseconds in the worst frame.

Changed: a spot is now every navmesh triangle under the bounds of the object that blocked (or under a 150-unit circle
when there is none), up to 16; the same object is one obstacle from whichever side. Two new host tests.

Also new: `NWalkerDetour 4` uses the navigation grid of todo 45 for the way round instead of the side-step.

To run again: `NWalkerDebug 1 1`, `NWalkerDetour 3`, the same walk. Expected: one line `the pathfinder is to avoid N
navmesh triangle(s) ... under the bounds of the object that blocks` with N above 2, red crosses all over the car's
ground, and a route that no longer passes the car (`is now ... m long`, clearly longer). If it says `under a circle
around the blocked spot`, the car's bounds could not be read: send the line.

## On by default (2026-11-12)

The designer does not want to enable features through commands: things should work, and a switch should turn them off
when something goes wrong. The path check is now on from the start in its normal form (grid, then rerouting).
`NWalkerDetour 0` switches it off until the game is closed, `NWalkerDetour 1` back on. The older forms are for
debugging: `2` side-step, `3` reroute only, `4` side-step off the navmesh. **The numbers changed**: `4` used to be the
grid and `2` the side-step off the navmesh; earlier sections of this file use the old numbers.

Checks from here on need no command. When a walk goes wrong, try it again after `NWalkerDetour 0`: if it works then,
the path check is the cause.

## Playtest 2026-11-12: still through the car

Designer: she still cannot reach the door and the route still goes through the car; the red marks appear but the
route does not change. From the log: the grid found no way round (correct), the reroute marked 16 triangles under
the object's bounds, the pathfinder used other triangles through the same gap (112 m -> 117 m), and the "same
obstacle" rule stopped any further marking.

Changed: a reroute loop (design note, "Second run"). The pathfinder is asked repeatedly; each time the triangles its
route uses at the blockage are marked, until a route is free of obstacles for 1500 units or the pathfinder routes
through marked ground. In the second case the walk fails at once with the new reason `Blocked` ("the way there is
blocked ... and no way around it was found"). One new host test for the text; the bounds-based marking was removed.

To run: nothing to switch on. `NWalkerDebug 1 1`, then the same walk. Expected in the log: `the grid found no way
round`, `Walker reroute: asking the pathfinder ...`, a `try N` line per attempt, then either `the route ... is free
for the next ... m` with a clearly longer route and she goes another way, or `no other way to ...` and the walk
fails within a second or two with the "blocked" message. Send the `Walker reroute` lines either way.

## Playtest 2026-11-12 (third run): a loop, and the underpass the pathfinder cannot see

Designer: an endless loop of "0 tries, 0 navmesh" reroute lines until she was moved by hand; and the pathfinder does
find the way round once she is near the underpass, but not from the wrecks.

From the log:
- The loop was a bug in the reroute loop of the previous change: it ignored rises, the regular check does not, and
  this block was a rise (the low end of a wreck). Each called the other every frame. Fixed with one shared test
  (`RouteBlocked`) and a guard: a reroute started four times in a row at one place fails the walk as blocked.
- After she was moved the loop ran as designed and failed the walk as blocked: the pathfinder has no other route.
- The reason is in the later walks: the way round is an underpass about 123 m south of the wrecks, three exterior
  cells away, one row outside the cells loaded around her. Its navmesh is not there for the pathfinder until she is
  further south. From the low ground west of the highway, two cells south, the route through it is found at once.

Open question for the designer (not built): what she should do when everything in the loaded area is blocked.
1. Fail at once with the "blocked" message, as now, and leave it to Neuro.
2. Head for the navmesh point nearest the target that she can reach with a ray-clear route, in another loaded cell
   (here: west down the slope), and look again from there; a few such hops, then fail. It would probably solve this
   place, and it would also send her on long walks to nowhere whenever a target really is cut off.
3. Read the game's own map of how navmeshes connect across unloaded cells and plan over that. The right tool, and
   a research task of its own.

Checks: `bash tools/test.sh` (382 pass), the Debug DLL builds. Not run in the game.
To run: the same walk from the wrecks. Expected: about five seconds of standing (the grid), then a few `Walker
reroute` lines ending in `no other way to ...`, and the "blocked" message to Neuro. No loop.
