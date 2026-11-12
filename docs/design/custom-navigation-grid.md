# Custom navigation grid from raycasts (todo 45): evaluation

Written 2026-11-10. The cost of a ray was measured in game the same day with `NWalkerProbe` (four spots outside
Primm); an interior has not been measured. Everything else is answered from the sources and from the ray counts of the
sweep that todo 48 already uses.

## Decision and what is built (2026-11-10)

I recommended not building the grid, because rerouting through the pathfinder covers an obstacle on a route that has
another way round. The designer decided to build it anyway: some areas have a very bad navmesh or none, and there the
pathfinder has nothing to reroute over. The first stage is built and waits for its first run in the game.

Built (`src/Movement/NavGrid.*`, engine-free, 14 host tests in `tests/NavGridTests.cpp`):

- **Cells** of 32 units, aligned to the world, with a level per 64 units of height so a deck over a road is another
  cell than the road. 32 keeps a ray within the player's radius (20.3) of everything between two neighbouring lines.
- **A step** from one cell to a neighbour (eight directions) is one `PathProbe::Sweep` at the player's width: floor
  rays, knee and chest rays on the centre line and to both sides, and the rays run the player's radius past the cell
  centre so a cell is only entered when its centre keeps that distance from what lies beyond. A diagonal step needs
  both straight steps beside it. Doors and the walk's own target block for the current search only.
- **Nothing is sampled ahead.** The A* search asks about a step when it reaches it and the grid remembers every
  answer, for as long as the player stays in that interior or worldspace. The same search again casts no rays.
- **Cost** of a route: its length, plus the height climbed, plus 15 % for every blocked step of a cell it leaves, so
  routes keep off walls a little.
- **A search** runs a ray budget at a time (1000 rays a frame, 5 ms) and gives up after 4000 cells, about 29 m x 29 m
  of ground. When it gives up it still returns the way to the cell nearest the goal, if that is a real step nearer.
- **Where she got stuck** on a grid route, the step she was taking is marked blocked whatever the rays said.

In the test world: 600 units of open ground 18 cells and 1,150 rays; round a car 300 units wide 140 cells and 8,900
rays (9 frames); out of a U-shaped pocket and round it 290 cells and 18,000 rays (18 frames); a wall with no way
round the full 4000 cells and 270,000 rays (4.5 s at 60 frames a second, 1.35 s of ray time).

Walker (`NWalkerDetour 4`): when the path check finds the route blocked, the grid is searched for a way from where
she stands to the point of the route 450 units beyond the blocked spot. She stands still while it searches. The
route found is walked waypoint by waypoint (skipping one when the next but one can be walked to directly) with the
same checks as a side-step, and when it is found only part of the way it is walked if it gets well past the near side
of the obstacle. When the grid finds nothing, the spot goes to the pathfinder to route around, as before. This also
works when the pathfinder has no route at all and the Walker steers in a straight line: each blocked stretch of that
line is walked around on the grid.

`NWalkerGrid [distance] [cells]` runs one search from the player to the point `distance` units ahead (default 600),
logs the outcome, cells, rays and time, and draws the route in the overlay. It is the way to look at the grid without
a walk.

Not built: saving and loading (below, section 3), sampling ahead of the player, and long routes on the grid alone.
A search covers about 29 m; across an area with no navmesh she advances in hops of that size toward the target and
can walk into a dead end the navmesh would have known about.

Unverified, all of it in game: that 32-unit steps checked with these rays are steps the player can really take; the
slope limit (floor normal z 0.6, about 53 degrees; the engine's own limit was not read); the rise and drop limits on
real ground; how often a search has to give up; what standing still for a search looks like on stream.

### In-game checks for the grid

With `NWalkerDebug 1`:
1. Open ground, `NWalkerGrid`. Expected: `a way to the point`, about 20 cells, a straight violet line to a white pole.
2. Facing the Primm car with the far side of it within 8 m: `NWalkerGrid 600`. Expected: a violet route round the
   car if there is one a person could walk; `no way` or `part of the distance` if the car really fills the gap. Send
   the `Walker grid:` lines either way.
3. Facing stairs, a slope, a doorway, a narrow gap between two objects: does the route go where you could walk, and
   only there?
4. `NWalkerGrid 600 8000` at a wall with no way round: note the milliseconds (the worst case).
5. `NWalkerDetour 4`, then a walk whose route is blocked by something she can walk around off the navmesh. Expected:
   she stops for a moment, `Walker detour 1 ...: the grid found a way round`, a violet chain of waypoints, she walks
   it without touching the obstacle.
6. The same at the Primm car: expected `the grid found no way round`, then the reroute lines.
7. A walk to something the pathfinder has no route to (the red straight line in the overlay): does she get around
   what is in the way?

## 1. Cost

Measured (2026-11-10, exterior, rays cast as LINEOFSIGHT, 2000 rays per line, four runs):

| Ray | Microseconds each (outside Primm) | (interior) |
| --- | --- | --- |
| 60 units, chest height | 3.7 to 4.0 | to measure |
| 400 units, chest height | 4.6 to 5.9 | to measure |
| 3500 units, chest height | 5.7 to 9.5 | to measure |
| 180 units down | 4.5 to 5.7 | to measure |
| 400 units with reference lookup | 4.6 to 6.0 | to measure |

The grid's rays are the short ones, so about 5 microseconds a ray, or 200 rays for 1 ms of a frame. Finding the
reference behind a hit costs nothing measurable, so every sample can say what it hit.

Ray counts, with a cell of 20 units (0.29 m; see "Resolution"):

| What | Cells | Rays per cell | Rays | Ray time in all |
| --- | --- | --- | --- | --- |
| 50 m x 50 m, every cell, 4 neighbours | 30,600 | 5 (1 floor, 2 heights to 2 neighbours) | 153,000 | 0.8 s |
| The same with diagonals sampled | 30,600 | 9 | 276,000 | 1.4 s |
| On-demand search around one obstacle (stage 2), 800 to 4000 cells reached | 800 to 4,000 | 5 to 9 | 4,000 to 36,000 | 0.02 to 0.18 s |

Spread over frames at 60 frames a second:

- 50 m x 50 m at 5 rays a cell: 13 s at 200 rays a frame (1 ms), 5 s at 500 rays a frame (2.5 ms).
- An on-demand search: 7 to 60 frames at 600 rays a frame (3 ms), so at most a second, usually a fraction of one.
- Keeping a 50 m wide strip sampled ahead of a walking player needs about speed x 50 m of new ground a second. At
  4 m/s (an assumption; the run speed was not read) that is 2,450 cells or 12,250 rays a second: about 200 rays or
  1 ms a frame.

So "builds itself while playing" costs about 1 ms a frame while she walks into new ground and nothing while she
stands or walks known ground, and the on-demand search is cheap. The numbers are from one exterior on the designer's
machine; a dense interior should be measured before stage 3.

### Resolution

The cell must not be larger than the player's collision radius, or a gap one cell wide could be narrower than the
player. The radius reads 20.3 units in game (`WorldUtils::PlayerCollisionShape`; the height reads exactly 128, which
backs the offset), so the cell is 20 units. Halving the cell quadruples every number above.

## 2. What a ray can tell

Source-backed (details and sources in `raycast-path-detours.md` and at `WorldUtils::CastRay`):

- A ray straight down gives the floor height, and with it the step between two neighbours (rise, drop, slope).
- A horizontal ray between two neighbours at knee and chest height gives blocking.
- A hit names the reference behind it and its collision layer (both confirmed in game). So a door can be marked as a
  door, an actor ignored, and a hit on something movable (layers CLUTTER, PROPS, DEBRIS) kept apart from a hit on a
  static or the terrain (the landscape reports layer GROUND and no reference).
- The surface normal is in the payload and reads as a unit vector matching the surface (confirmed in game), which
  gives the slope without a second ray.

Limits:

- A ray is a line and the player is a capsule. Things thinner than the spacing between rays slip through: a post
  between two grid lines, a single rail between knee and chest height, a low beam above chest height. A finer grid and
  more heights reduce this and cost rays.
- Havok can sweep a whole shape along a line, which would answer "can the capsule move from here to there" exactly.
  No reference documents an engine function for it, so it is not available without new reverse engineering.
- Which layer to cast as is open. In game, rays cast as LINEOFSIGHT and CHARCONTROLLER hit the same statics, terrain
  and NPC; they have not been compared at glass, chain-link or clutter, where they could differ. Cast as PATHPICK a
  ray goes through actors, which a grid wants (an actor must never mark a cell blocked); what else it goes through is
  not known.
- Water, trigger volumes and scripted blockers (collision that appears with a quest stage) are invisible or change
  without notice.

## 3. Storage (not built)

- **Key:** worldspace form ID plus exterior cell coordinates, or the interior cell's form ID. One file per cell under
  the plugin's own folder next to the DLL, written when the cell unloads.
- **Layout:** a cell is 4096 units, so 205 x 205 columns at 20 units. A column holds one entry per floor level
  (buildings and bridges have several): height (16 bits), edge bits (8), flags (8). About 170 KB a cell and level
  before compression, mostly runs of the same value. A whole playthrough's visited cells would be around a hundred
  megabytes uncompressed, so compression is not optional.
- **Stale data:** a header with the format version and a hash of the load order catches a changed mod list. It does
  not catch a mod updated in place, a quest that enabled or disabled an object, or a door. The answer is not a better
  fingerprint but to treat the file as a hint: the live sweep of todo 48 still runs along every route, a stored free
  edge that the sweep finds blocked is corrected, and a stored blocked edge is sampled again when a search wants it.
- **What is stored:** only hits on statics and terrain. Doors are stored as doors, never as blocked. Hits on actors
  are never stored; hits on movable objects are kept in memory with a time and forgotten after about 30 s.

## 4. Hybrid

Navmesh for the long route, grid only for the stretch around an obstacle:

- The pathfinder stays the only source of the long route. It knows doors, load doors and other cells; a local grid
  knows none of that, and nothing is sampled far away.
- The grid search starts where todo 48's check finds the route blocked, runs from the player's cell to the point of
  the old route 450 units beyond the blocked spot, and returns a list of waypoints instead of todo 48's single one
  (built).
- The Walker follows them with the code todo 48 added (steer at a waypoint, drop it on arrival, when the route is
  free again, or when recovery takes over). Stuck, slide, progress and the recovery ladder are unchanged.
- A switch like `NWalkerDetour` selects plain navmesh, the one-waypoint detour or the grid, for comparison in the
  same spot.

The grid alone for short walks is not recommended: it would need the whole area sampled before the first step and
gives up the navmesh's knowledge of doors.

## 5. Dynamic objects

- Doors: marked, passable at a cost, opened by the existing door recovery.
- Actors: ignored by every ray (as in todo 48).
- Pushed clutter: transient marks that decay.
- Stuck anyway: mark the cells in front of the stuck spot blocked for that walk and search again. This is the grid
  version of the Wander step and is what would let the recovery ladder shrink later.

## 6. Prior art

- **FalloutNVAccess** (`docs/autowalk.md`, MIT, revision d24c62e): its player autowalk first steered in a straight
  line and got stuck at walls and furniture, then read navmesh waypoints, and finally hooked three call sites so the
  engine's own NPC movement tick runs for the player, because it wanted the engine's door handling and "obstacle
  avoidance and repath". Its notes mark that last step "implemented, needs testing". That is a third route besides
  rays and a grid: drive the player through the NPC mover instead of keys. It would replace the Walker's movement
  wholesale, depends on addresses documented only in those notes, and nothing says the NPC avoidance handles a static
  that is missing from the navmesh. Not pursued; worth remembering if both ray approaches disappoint.
- **itr-nvse** (MIT, revision bb61633): the pathing calls already adapted under `src/itr/`, the ray payload layout,
  and ground rays cast as layer 6 (`commands/GroundCommands.cpp`, `handlers/OnFootContactHandler.cpp`).

## What the grid is built from

- `PathProbe::Sweep` (`src/Movement/PathProbe.*`): "can the player walk from this ground point to that one", with
  floor following, rise, drop, doors, actors and the target told apart. A grid edge is one call of it (or a leaner
  two-ray version of it, see the cost table).
- `PathProbe::World`: the ray interface with a box-world fake for host tests, so a grid and its A* can be developed
  and tested on the host like the rest of `src/Movement/`.
- `DetourPlanner`'s per-frame ray budget and `DetourTracker`'s waypoint handling, budget and side memory.
- The overlay's ray, blocked-spot and waypoint drawing.

## Decisions for the designer

1. After the first run: is 5 ms a frame acceptable while a search runs, and standing still for it?
2. Should the grid be saved to files (section 3)? It would spare the rays the second time through a place; the
   in-memory grid already does that within one visit.
3. Should it sample around the player while she walks, so a search finds the ground already known?
4. Long routes on the grid alone, for whole areas without navmesh: wanted, or are hops of 29 m enough?
