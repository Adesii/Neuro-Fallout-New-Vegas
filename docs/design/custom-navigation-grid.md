# Custom navigation grid from raycasts (todo 45): evaluation

Written 2026-11-10. Nothing was measured in the game: the cost numbers this evaluation needs come from the
`NWalkerProbe` console command built with todo 48, and the table in "Cost" is waiting for them. Everything else is
answered from the sources and from the ray counts of the sweep that todo 48 already uses.

## Recommendation

Do not build the full grid yet. Build it in stages, and only as far as the game shows is needed:

1. **Now (done, todo 48):** the ray sweep along the navmesh route and a one-waypoint detour. Test it at the Primm car.
2. **If obstacles remain that one waypoint cannot get around** (a car against a fence, a U-shaped barricade): a small
   grid built on demand around the blocked spot, searched with A*, sampling a cell only when the search reaches it.
   Nothing is saved, nothing is sampled ahead of time. This replaces step 3 of todo 48 (the twelve fixed points) and
   keeps everything else.
3. **Only if stage 2 is too slow or too forgetful in practice:** keep the sampled cells in memory while the area is
   loaded, then save them per cell.
4. **Not recommended at all:** the grid alone for short walks, or a grid that samples everything around the player
   all the time. The navmesh stays the long-range route in every stage.

Reasons: the one failure seen so far is an object standing on a good route, which stage 1 targets directly; a ray
cannot tell everything the grid would need (see "What a ray can tell"), so the grid's quality is unknown until the
sweep has been watched in game; and a saved grid goes stale in ways that are hard to detect.

## 1. Cost

What the game can afford per frame is one number: the cost of a ray. Run `NWalkerProbe` (optionally with a ray count,
default 2000) in Primm and in an interior and fill in:

| Ray | Microseconds each (Primm) | (interior) |
| --- | --- | --- |
| 60 units, chest height | to measure | to measure |
| 400 units, chest height | to measure | to measure |
| 3500 units, chest height | to measure | to measure |
| 180 units down | to measure | to measure |
| 400 units with reference lookup | to measure | to measure |

The grid's rays are the short ones (a floor ray and rays to the neighbours), so the first and fourth rows decide.

Ray counts, independent of that number. With a cell of 32 units (0.46 m; see "Resolution"):

| What | Cells | Rays per cell | Rays |
| --- | --- | --- | --- |
| 50 m x 50 m, every cell, 4 neighbours | 12,000 | 5 (1 floor, 2 heights to 2 neighbours) | 60,000 |
| The same with diagonals sampled | 12,000 | 9 | 108,000 |
| The same with todo 48's sweep per edge (its own floor samples, 3 rays wide) | 12,000 | 32 | 384,000 |
| On-demand search around one obstacle (stage 2), 300 to 1500 cells reached | 300 to 1,500 | 5 to 9 | 1,500 to 13,500 |

At a budget of B rays a frame and 60 frames a second:

- 50 m x 50 m at 5 rays a cell takes 60,000 / (60 x B) seconds: 5 s at 200 rays a frame, 2 s at 500.
- An on-demand search takes 5 to 45 frames at 300 rays a frame (todo 48's budget), under a second.
- Keeping a 50 m wide strip sampled ahead of a walking player needs about speed x 50 m of new ground a second. At
  4 m/s (an assumption; the run speed was not read) that is 950 cells or 4,750 rays a second, about 80 rays a frame.

Frame time is B x the measured cost. If a short ray costs 5 microseconds, 200 rays are 1 ms; at 20 microseconds they
are 4 ms. So "builds itself while playing" is affordable if a short ray is around 10 microseconds or less, and the
on-demand search is affordable almost regardless.

### Resolution

The cell must not be larger than the player's collision radius, or a gap one cell wide could be narrower than the
player. The radius is not known from two sources: JIP-LN-NVSE names `bhkCharacterController::shapeRadius` (0x5F8), and
`NWalkerProbe` logs it. 32 units is a placeholder until that line has been read in game. Halving the cell quadruples
every number above.

## 2. What a ray can tell

Source-backed (details and sources in `raycast-path-detours.md` and at `WorldUtils::CastRay`):

- A ray straight down gives the floor height, and with it the step between two neighbours (rise, drop, slope).
- A horizontal ray between two neighbours at knee and chest height gives blocking.
- A hit names the reference behind it and its collision layer. So a door can be marked as a door, an actor ignored,
  and a hit on something movable (layers CLUTTER, PROPS, DEBRIS) kept apart from a hit on a static or the terrain.
- The surface normal is in the payload (one source, itr-nvse), which would give the slope without a second ray.

Limits:

- A ray is a line and the player is a capsule. Things thinner than the spacing between rays slip through: a post
  between two grid lines, a single rail between knee and chest height, a low beam above chest height. A finer grid and
  more heights reduce this and cost rays.
- Havok can sweep a whole shape along a line, which would answer "can the capsule move from here to there" exactly.
  No reference documents an engine function for it, so it is not available without new reverse engineering.
- Which layer to cast as is open. LINEOFSIGHT is known to work but lets vision-transparent things through (glass,
  chain-link). CHARCONTROLLER should match what the player collides with; whether a ray can be cast as it is a
  measurement (`NWalkerProbe` prints both side by side).
- Water, trigger volumes and scripted blockers (collision that appears with a quest stage) are invisible or change
  without notice.

## 3. Storage (stage 3 only)

- **Key:** worldspace form ID plus exterior cell coordinates, or the interior cell's form ID. One file per cell under
  the plugin's own folder next to the DLL, written when the cell unloads.
- **Layout:** a cell is 4096 units, so 128 x 128 columns at 32 units. A column holds one entry per floor level
  (buildings and bridges have several): height (16 bits), edge bits (8), flags (8). About 64 KB a cell and level
  before compression, mostly runs of the same value. A whole playthrough's visited cells would be in the tens of
  megabytes uncompressed.
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
- The grid search starts where todo 48's check finds the route blocked, runs from the player's cell to any point of
  the old route 5 to 8 m beyond the blocked spot, and returns a list of waypoints instead of todo 48's single one.
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

## What exists already

- `PathProbe::Sweep` (`src/Movement/PathProbe.*`): "can the player walk from this ground point to that one", with
  floor following, rise, drop, doors, actors and the target told apart. A grid edge is one call of it (or a leaner
  two-ray version of it, see the cost table).
- `PathProbe::World`: the ray interface with a box-world fake for host tests, so a grid and its A* can be developed
  and tested on the host like the rest of `src/Movement/`.
- `DetourPlanner`'s per-frame ray budget and `DetourTracker`'s waypoint handling, budget and side memory.
- The overlay's ray, blocked-spot and waypoint drawing.

## Decisions for the designer

1. After the Primm test of todo 48: is the one-waypoint detour enough for now, or go to stage 2?
2. With the measured cost: is 1 ms a frame acceptable for sampling during a walk? That sets the ray budget.
3. Should a saved grid (stage 3) exist at all, given that Neuro's playthrough visits most places once?
