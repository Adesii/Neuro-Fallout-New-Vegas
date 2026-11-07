# 43 - In-game Walker debug mode: show the computed path

Status: open
Area: Walker / tooling (`src/WalkerHandler.cpp`, new debug module, plugin command)
Source: designer's play-testing, 2026-11-07 (Primm)
Related: `44-debug-camera.md` (seeing the path from above), `45-custom-navigation-grid.md` (the longer-term fix)

## Problem
In Primm the Walker runs straight into objects. Most of them are not part of the navmesh, yet in the GECK the navmesh
does have a gap around them, so a navmesh route should not lead into a wall. Nobody can tell from the log alone what
route the Walker actually followed, so a debug mode that can be switched on in game is needed before anything is fixed.

## Goal
A debug mode the designer can switch on and off in game that shows what the Walker is doing: the path nodes it got
from the pathfinder, the node it is steering at right now, and whether it fell back to steering straight at the target.

## Ideas (designer)
- Draw the route: a small cube or marker at every navigation waypoint, with a line between consecutive waypoints.
- Mark the node currently steered at, and the Wander or detour spot during recovery.
- Combine it with a free or top-down camera (todo 44) so the whole route is visible from the first node on.

## Suspects worth showing (from reading `WalkerHandler.cpp`, not confirmed)
- `ResolveNavigationPoint` falls back to the target's own position when `BuildPath` fails twice (directly, then to the
  closest navmesh triangle). The Walker then walks a straight line into whatever is in between. The overlay should show
  when this fallback is used, and the log should say so once per route.
- `BuildPathPoint` steers at the first node more than `kWaypointReachedDistance` (60 units) away and skips the rest. On
  a route that bends tightly around an obstacle, skipping can cut the corner into it.
- The route is rebuilt every 0.25 s from wherever the player stands. If the player stands on an object's collision,
  off the navmesh, the search may start from the wrong triangle or fail.
- `GetNavigationCell` and the waypoint-marker trick (`g_state.waypointMarker->pos = destination`): check that the search
  uses the cell and navmesh the player is really in, near exterior cell borders too.

## Work
1. Toggle: a plugin console command (`DEFINE_NEURO_COMMAND_PLUGIN`, so it gets the `N` prefix, for example
   `NWalkerDebug 1`), optionally also a debug-only hotkey. Off by default, never on for Neuro's normal play.
2. Data: keep the last `PathResult` nodes, the chosen index, the fallback flag and the recovery spot in the Walker
   state (no behaviour change), and log them when the route changes while debug is on.
3. Drawing, research first (`docs/reference-research.md`). No reference plugin in `.reference-data/repos` has a
   world-space line or debug-shape drawer (searched for WorldToScreen/DrawLine/DebugDraw). Options to evaluate:
   - Place temporary marker references (a visible base form) at the nodes and move or delete them. This needs a
     visible base form: vanilla or a new one in `Neuro_FNV.esm`. The ESM is binary and the repo has no command that
     rebuilds it.
   - Draw lines and boxes in a Direct3D 9 hook (for example EndScene), projected with the engine's scene camera. This
     needs the camera matrices from the JG/NVSE definitions. Never invent offsets.
   - Text-only first step: on-screen or console output of the node list and distances.
4. Engine-free parts (path summary, what changed between two routes) go beside `WalkerLogic` with host tests.

## Questions for the designer
- Console command, hotkey, or both? Should the overlay stay on across loads?
- Is a markers-only first version (no lines) useful enough to start with?

## Done when
With debug on, the designer can walk the Primm spots and see each route the Walker used, the node it steered at, and
whether it fell back to a straight line. The mode costs nothing when it is off.
