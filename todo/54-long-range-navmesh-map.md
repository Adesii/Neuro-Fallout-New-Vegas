# 54 - Plan long routes over the game's map of navmeshes (research)

Status: open (research first; nothing built)
Area: Walker / pathing (`src/itr/` pathing, `src/WalkerHandler.cpp`)
Source: designer, 2026-11-12, from the Primm wrecks (todo 48, third run)
Related: `to_verify/48-raycast-path-detours.md` (the reroute loop and the hops this would replace or back up),
`45-custom-navigation-grid.md`

## Problem
The engine's pathfinder only searches the navmesh of the cells that are loaded around the player (usually five by
five exterior cells). At the wrecks north of Primm the highway is blocked, and the way round is an underpass about
123 m further south, one row of cells outside what is loaded. So from the wrecks the pathfinder has no other route,
however many navmesh triangles are marked as avoided, and the Walker can only fail as "blocked" or guess where to go
(the hops of todo 48: head for the reachable navmesh point nearest the target in another loaded cell and look again
from there). From two cells further south the same pathfinder finds the underpass at once.

The Walker's ordinary long walks have the same root: when there is no path to the target itself it heads for the
navmesh point nearest the target in the player's own cell, cell after cell, in a straight-line sense. That works when
the straight line is roughly the way, and not when the way leads away first.

## Idea
The game keeps a map of every navmesh in the game, loaded or not, with which navmeshes border which and which doors
connect them. Plan the long route over that map: a chain of navmeshes from where she stands to the target's, skipping
a connection the Walker has found blocked. Then walk it one loaded stretch at a time with the pathfinder, the ray
check and the grid as now.

## Starting points (read, not verified in game)
- JohnnyGuitarNVSE `JG/internal/Game/Bethesda/NavMeshInfoMap.hpp` / `.cpp`: `NavMeshInfoMap` (a form) with
  `kNavMeshInfos` (form ID to info) and `kWorldNavMeshInfos` (worldspace, then cell coordinates, to the infos of that
  cell); `FindNavMeshInfoForCellKey` 0x6B7850, `FindNavMeshInfoForInteriorCell` 0x6B78B0, `FindNavMeshInfoForCell`
  0x6B77E0.
- `NavMeshInfo.hpp` (size 0x5C): `uiNavMeshID`, `uiCellCoords`, `kApproxLocation`, `kAdjacentNavMeshes`,
  `kPreferredAdjacentNavMeshes`, `kConnectedDoors`, flags (`BLOCKED`, `DISABLED`, `DOOR_PORTAL_ONLY`, `HAS_ROAD`),
  `pNavMesh` (null when not loaded).
- This project already calls `Pathing::GetPotentialNavMeshInfoForLocation` (0x6D6F40) in `src/itr/NavmeshCommands.cpp`
  and reads a path solution's `virtualNodes` field in `src/itr/PathingShared.h` without using it.
- The game executable is on this machine and can be disassembled to check any of this (see the memory note on
  verifying engine facts).

## Questions to answer
1. Where is the `NavMeshInfoMap` singleton, and are the infos of unloaded cells really there with their neighbours
   filled in? Is one info one navmesh, and how many are there per exterior cell?
2. Does the engine already plan over this map? A path solution has "virtual nodes" and a first and last "loaded
   virtual node" index. Find out what `PathManager::Solve` returns for a target in an unloaded cell, what
   `incompletePath` means, and whether a request can ask for a partial route. If the engine does the long-range part
   itself, the Walker may only have to stop rejecting such solutions and follow them.
3. How coarse is the map? A navmesh as big as a cell cannot say that the highway inside it is blocked while the
   ground beside it is not. Is a border between two navmeshes described well enough (which edge, where) to mark one
   crossing as blocked and keep another?
4. What does a step over the map cost in walking distance? `kApproxLocation` is one point per navmesh; is there
   anything better for a cost?
5. Doors: `kConnectedDoors` links interiors and exteriors. Could the same plan replace the "head for the door that
   leads toward the quest target" logic, or should it stay separate?
6. How does the result reach the pathfinder? As a target point inside the next navmesh of the chain (a better version
   of the hops), or as avoid nodes that push it the right way?

## Work (after the research)
1. Engine-free core in `src/Movement/` with host tests: a graph of navmesh nodes and connections, a shortest-route
   search that can skip blocked connections, and choosing the next point to walk to.
2. Engine side: reading the map into that graph (once per worldspace, it does not change), and marking a connection
   blocked when the reroute loop of todo 48 finds no way in the loaded area.
3. Walker: use the chain for the snap target instead of "nearest navmesh point in my cell", on by default, with the
   hops of todo 48 as the fallback when the map gives nothing.

## Done when
A design note `docs/design/long-range-navmesh-map.md` answers the questions with evidence, and the designer has
decided whether to build it. If built: from the wrecks north of Primm she walks to the underpass and into Primm
without hops.
