# 48 - Check the path ahead with raycasts and detour around what blocks it (evaluation)

Status: open (evaluate before building)
Area: Walker / pathing (`src/WalkerHandler.cpp`, `src/Movement/`, `src/utils/WorldUtils.*` rays)
Source: designer, 2026-11-08, from the todo 43 debug overlay in Primm
Related: `to_verify/43-walker-debug-path-overlay.md` (where this was found; draw the probes there),
`45-custom-navigation-grid.md` (the heavier alternative), `to_verify/25-walker-stuck-recovery.md` (Wander recovery)

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
