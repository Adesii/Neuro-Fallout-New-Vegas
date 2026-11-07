# 43 - In-game Walker debug mode: show the computed path

Status: to verify
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

Not answered before implementing; defaults chosen (change them if they are wrong): console command only, no hotkey;
the on/off state survives loads but not a game restart (off at start-up); lines and markers together, since they cost
the same to draw.

## Done when
With debug on, the designer can walk the Primm spots and see each route the Walker used, the node it steered at, and
whether it fell back to a straight line. The mode costs nothing when it is off.

## Result (2026-11-08)

Built:
- **Toggle:** console command `NWalkerDebug` (toggle), `NWalkerDebug 1` (on), `NWalkerDebug 0` (off). It prints the
  colour legend to the console. Registered after `NSendContext`, so that command's opcode (stored in the ESM's compiled
  scripts) is unchanged; `NWalkerDebug` takes the next opcode, 0x4298.
- **Capture** (`src/WalkerHandler.cpp`, no behaviour change): at every route search while debug is on, the full
  `PathResult` node list, the node steered at, the point steered at, how it was found (`close`, `navmesh path`, `path
  to the navmesh point next to the destination`, `STRAIGHT LINE`), the closest-navmesh point when the snap fallback
  ran, the Wander spot during recovery, the player's position, and suspects 3 and 4: whether the player stood on the
  navmesh (`Pathing::PointOnNavmesh` at the player) and the form IDs of the search cell (`GetNavigationCell`), the
  waypoint marker's `parentCell`, the player's cell and the target's cell. Detour probes in `PickDetour` are not
  recorded. The capture is kept apart from the route state, so a failed or finished walk stays on screen (dimmed)
  until the next command, a load, or a cell change to another place.
- **Log:** `Walker debug route to X: ...` in `neuro-fnv.log` whenever the route changes (mode, node count, chosen node,
  or any node but the first moved more than 30 units), with every node's position. The console gets one line per mode
  change. Independently of debug mode, the two fallbacks are now logged once per route: `Walker found no path to X
  itself; heading for the closest navmesh point (...)` and the warning `Walker found no path to X ...; steering in a
  straight line at (...)`.
- **Overlay** (`src/Debug/WalkerOverlay.cpp`): a Direct3D 9 Present hook, installed on the first `NWalkerDebug 1`
  (nothing is hooked before; when off the hook only checks a flag). It projects through the world camera's
  `m_aafWorldToCam`/`m_kPort` and draws with no depth test, so the route shows through walls:
  - cyan line: navmesh path; orange: path to the navmesh point next to the target (orange frame at that point); red
    thick line: straight-line fallback; thin white: within reach, steering directly;
  - yellow squares with short poles and node numbers at every path node; a thin line from the last node to the
    destination (a gap the path does not cover);
  - green: the point steered at (frame and pole) and a line from the player to it;
  - white frame and tall pole: the command's target; magenta square and pole: a Wander spot;
  - blue: the trail the player actually walked (a point every 25 units, last 800).
- **Engine-free, host-tested:** `src/Debug/OverlayMath.*` (projection, near-plane clipping in clip space before the
  divide, screen clipping, thick lines, boxes, seven-segment digits) and `src/Movement/RouteDebug.*` (route data,
  change detection, log text, trail, overlay scene). Tests: `tests/OverlayMathTests.cpp`,
  `tests/RouteDebugTests.cpp`.

Evidence: renderer singleton 0x11F4748 (JG `internal/Game/Gamebryo/NiRenderer.cpp`, JIP-LN-NVSE
`internal/netimmerse.h`), `NiDX9Renderer::device` at 0x288 (JG and JIP `internal/netimmerse.h`), `NiCamera` matrix at
0x9C and viewport at 0x100 (JG `internal/netimmerse.h` with its size assert, JIP `NiCamera::WorldToScreen` in
`internal/netimmerse.cpp`), Present = vtable slot 17 (declaration order in the Windows SDK `d3d9.h`). No reference code
was copied; the projection maths are written here.

Checks run: `bash tools/test.sh` (all pass, 20 new cases), `clang-format`, `env -u FalloutNVNeuroPlugin bash
tools/build.sh Debug` (builds, no warnings in the new files). Nothing was run in the game; the built DLL is not
deployed.

Unverified (needs the game):
- That the game shows frames through `IDirect3DDevice9::Present` (and not a swap chain's Present). The log tells the
  failures apart, each line once: `Present hook installed` (hook written), `Present reached the hook` (the game calls
  it), `no usable world camera at Present` (camera missing), `a route exists but nothing projected` (with the
  matrix's w row and the player's clip w: all-zero or negative w means the matrix is not what was assumed), and
  `first frame drawn` (it works).
- That the world camera's matrix read at Present is the one the world was drawn with. JG only trusts it while the
  camera's FOV equals the player's world FOV, so it may be rewritten mid-frame (viewmodel, scope). Symptom: lines that
  slide against the ground while turning, or shift when the weapon is drawn.
- That Present runs on the main loop thread (the first-draw log line prints both thread IDs; the shared view is
  locked anyway).
- Saving and restoring the device state with a state block: if the device is a pure device the log warns, and the
  game could render oddly after the overlay draws.
- Behaviour with other Present hooks (ReShade, ENB, Steam overlay, DXVK HUD).
- The overlay also draws over menus and the Pip-Boy while it has something to show.

Manual in-game checks:
1. Load a save, open the console, run `NWalkerDebug`. Expected: the legend line, and `Walker debug mode on.` plus
   `Present hook installed` in `neuro-fnv.log`. Run it again: off, the overlay disappears.
2. With debug on, give Neuro a move to something about 20 m away in the open. Expected: a cyan route with numbered
   yellow nodes, a green steering point a little ahead, a white pole on the target, a blue trail behind the player,
   and `Present reached the hook` then `first frame drawn` in the log once. If nothing shows, send the
   `Walker debug overlay:` lines from the log: they say which step stopped it.
3. Turn the view left and right and walk. Expected: lines stay glued to the ground. Draw and holster the weapon, zoom
   with a scope: nothing jumps. Symptom of failure: lines drift or scale with the view.
4. The Primm spots from the 2026-11-07 playtest. Expected: the overlay shows whether the route itself leads into the
   object (cyan line through it), whether the Walker cuts the corner (green point skipping a bend), or whether it
   fell back to a red straight line; the log has the matching `Walker debug route` and fallback lines. Note the
   `on navmesh` value and the four cell IDs when it goes wrong, especially near exterior cell borders.
5. A walk that fails (blocked, or the progress stall). Expected: the route and trail stay on screen, dimmed, after
   the failure. A new command replaces them.
6. A stuck walk that reaches a Wander recovery step. Expected: a magenta spot and the route to it.
7. Walk through a load door. Expected: the old route and trail disappear in the new cell.
8. Alt-tab out and back (device reset), and load another save. Expected: no crash, no stuck render state; after the
   load the overlay is empty until the next walk.
9. `tfc` with debug on: the overlay should follow the free camera (useful for todo 44).
10. With debug off for the whole session (never switched on): no `Present hook` line in the log, Walker behaves as
    before.
