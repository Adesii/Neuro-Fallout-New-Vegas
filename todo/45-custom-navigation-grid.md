# 45 - Custom navigation grid from raycasts, with A* (evaluation)

Status: open (evaluated 2026-11-10; recommendation: do not build, rerouting in todo 48 covers the Primm car; designer's decision pending)
Area: Walker / pathing (`src/Movement/`, `src/WalkerHandler.cpp`, `src/itr/` pathing)
Source: designer, 2026-11-07
Related: `to_verify/43-walker-debug-path-overlay.md` (find out first why the navmesh route fails),
`to_verify/25-walker-stuck-recovery.md`, `to_verify/48-raycast-path-detours.md` (the lighter step, built first)

## Problem
The Walker relies entirely on the game's navmesh (`Pathing::BuildPath`, adapted from itr-nvse). New Vegas's navmesh is
known to be lacking in many areas, and in Primm many objects are not in it, so the player walks into them. Relying on
the navmesh alone will keep producing walks that fail or run into walls.

## Goal (designer's idea)
A custom navigation layer for the player, built from raycasts, used where the navmesh is not good enough:
- A grid of raycasts around the player marks which cells are walkable and which are blocked (collision, steps that are
  too high, drops).
- The grid can be saved to a file and loaded again, per cell or worldspace, so known areas need no new sampling.
- A simple A* over the grid finds the way between points where the navmesh is not enough.
- Ideally the grid builds itself while playing, around wherever the player goes, if that is cheap enough. It could then
  be a general solution, not only a patch for bad spots.

## Questions to answer in the evaluation
1. Cost: how many raycasts per frame the game can afford (the Walker already casts line-of-sight rays through
   `WorldUtils::HasLineOfSight`). Measure the frame time of a burst of rays. Grid resolution against the player's
   collision radius. How long an area of, say, 50 x 50 m takes to sample spread over frames.
2. What a ray can tell: a downward ray per grid point gives the floor height and slope, and horizontal rays between
   neighbours at knee and chest height give blocking. Check which collision layers the existing ray uses and whether
   it hits clutter, actors and doors (doors should be marked, not treated as walls).
3. Storage: file format and location (per worldspace and exterior cell, or per interior cell), how to detect a stale
   grid (a mod changed the cell, a door opened), size on disk.
4. Hybrid: navmesh for the long route and the grid for the last metres and around obstacles, or the grid alone for
   short walks. How it hands over to the Walker's existing steering, recovery ladder and progress checks.
5. Dynamic objects: doors, actors and pushed clutter change. Re-sample locally when the Walker gets stuck, mark grid
   cells blocked from the stuck spot, and decay old samples.
6. Prior art to read, as ideas only (check each license before copying anything): FalloutNVAccess
   (`.reference-data/repos/FalloutNVAccess`, MIT) documents its player autowalk on the game's navmesh and NPC pipeline
   in `docs/autowalk.md`, including that straight-line steering gets stuck at furniture. itr-nvse for the pathing calls
   already adapted under `src/itr/` (keep `src/itr/itr-license.md`).

## Work (after the evaluation)
1. Engine-free core in `src/Movement/` with host tests: grid storage, A* (8-neighbour, cost by slope and closeness to
   blocked cells), path smoothing, and the save/load format.
2. Engine side: a sampler that spreads raycasts over frames around the player, with a hard per-frame budget, behind a
   setting, and drawn by the debug overlay from todo 43.
3. Walker integration behind a switch, so it can be compared with the plain navmesh in the same spots.

## Done when
There is a written evaluation (cost numbers, what rays can and cannot detect, storage plan, recommended design) in
`docs/design/custom-navigation-grid.md`, and the designer has decided whether to build it.

## Evaluation (2026-11-10)

Written: `docs/design/custom-navigation-grid.md`. It answers questions 2 to 6 from the sources and question 1 from ray
counts and the measured cost of a ray. Nothing of the grid itself was built.

Recommendation in short: do not build the full grid yet. Todo 48 (built, to verify) covers the case seen in Primm. If
obstacles remain that one waypoint cannot get around, the next step is a small grid sampled on demand around the
blocked spot and searched with A* (nothing saved, nothing sampled ahead); saving and sampling while playing come
last, if at all. The navmesh stays the long-range route in every stage.

Already there from todo 48 and reusable: the walkability test between two ground points (`PathProbe::Sweep`), the ray
interface with a host-test fake, the per-frame ray budget, waypoint following, and the overlay drawing.

Measured 2026-11-10 (the designer's `NWalkerProbe` run outside Primm; numbers in the design note): a ray costs about
5 microseconds, the reference lookup nothing measurable; the collision radius reads 20.3 units, so the grid's cell is
20 units; reference, layer and normal of a hit all read correctly. With that, 50 m x 50 m is 153,000 rays (0.8 s of
ray time, 13 s at 1 ms a frame), an on-demand search around one obstacle at most a second, and sampling ahead of a
walking player about 1 ms a frame. Cost no longer argues against any stage.

Still needed before the designer can decide:
1. The Primm test of todo 48: does one waypoint get her round the car? That decides whether stage 2 is needed.
2. The same probe once in an interior, and at a fence or window (which layer the grid would cast as).

Decisions for the designer: the three at the end of the design note.

## After the Primm car test (2026-11-10)

The designer's test showed the car fills the only way through; the other ways into Primm are far off. A grid around
the obstacle would not have found them either. The answer built instead is rerouting through the engine pathfinder's
avoid nodes (todo 48, design note "The Primm car test and rerouting"). The recommendation is now: do not build the
grid unless a case turns up where the navmesh has no other way but a walkable gap exists off the navmesh.
