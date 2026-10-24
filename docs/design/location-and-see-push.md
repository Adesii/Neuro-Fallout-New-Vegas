# Location notices and the throttled "you see" push (todo 28)

## Goal
Neuro learns where she is, and hears about the few notable things that come into range, without a per-object stream.
Decisions are the designer's (todo 28, 2026-10-21).

## Skyrim behaviour (Neuro-Skyrim-Integration, read-only; idea source only)
- `[Current location: X ...]` (`src/main.cpp`, ~1861-1863) is the main "where am I" cue. With the walker active it is sent
  at once, otherwise it joins the delayed message queue and is merged with other pending text. The line also carries
  advice (gear, trespassing); the designer wants none of that here.
- "You see" notices (`Observer.cpp:3599-4036`) are a per-object stream with silent/non-silent chosen per object type.
  The designer wants only a throttled summary.
- Why (inferred): location changes are rare and important, while the surroundings are only worth a mention when something
  that matters appears. Does not transfer: Skyrim's location records and worldspace special cases.

## Contract
Plain `context`, silent or not. Nothing here is an action; no ids are published (they could be stale before she acts).

## Design
Location (`src/Context/Location.*`, engine-free, `tests/LocationTests.cpp`):
- `DescribePlace` turns an observation into a phrase: the cell's own name ("in Goodsprings Saloon") when it has one;
  otherwise "near <nearest map marker within ~300 m (21000 units)>"; otherwise "in the wasteland" (outdoors) or "in an
  interior area" (an unnamed interior).
- `PlaceTracker`: a place must be unchanged for 1.5 s before it is announced (load screens and cell crossings flap) and
  differ from the last announcement; a change inside the same space (outdoors, where only the nearest marker changes)
  also waits 30 s so walking along a settlement's edge stays quiet. A different space (door, load) is announced as soon
  as it is stable. The line is non-silent: "You are now in/near X." and carries no advice.
- Engine side (`GameplayHandler.cpp`): every 0.5 s while gameplay is not blocked (so after a load screen, never during
  it), `ReadPlace` reads the cell (`TESObjectCELL::IsInterior`, `fullName`, `GetWorldSpace`) and, outdoors in an unnamed
  cell only, collects map markers: references of base form 0x10 with `ExtraMapMarker` whose data has the Visible flag
  (JG `MapMarkerData.hpp`, JIP-LN-NVSE `GameExtraData.h` and `ln_fn_map_marker.h`), from the loaded grid and from the
  worldspace's persistent cell (`TESWorldSpace::pPersistentCell`). Only markers the map shows count, so the name is
  something a human player would know. All marker types count as core locations (cities, settlements, camps, landmarks,
  caves, ruins, vaults...). The greeting and the post-reconnect status carry the current place
  (`GameplayHandler::BuildGreeting`).
- "You see" (`src/Context/SeePush.*`, `tests/SeePushTests.cpp`): every 2 s, unless a fight, a force or the Pip-Boy driver is
  active (menus already stop `Process`), the loaded references within 1500 units (~21 m) are scanned with the shared
  classifier and scorer (`WorldUtils::ClassifyObject`, `Objects::Tier`, todo 07/35). Notable means a quest target (or the
  door toward one), a container, or a living actor. At most one summary per 20 s, at most 5 entries, most important first,
  each form id reported once per space (interior cell or worldspace; a door or a load starts over), names and distances
  but no ids, silent unless it contains a quest target. Entries that did not fit wait for the next summary.

## Unverified assumptions
- Which cell holds the map markers (the code reads both the grid and the persistent cell), and that the Visible flag
  means "discovered". If no marker is ever found, every outdoor place without a cell name reads "in the wasteland".
- That `TESObjectCELL::fullName` is empty for most wilderness cells and set for settlement cells.
- 300 m, 1.5 s, 30 s, 20 s, 1500 units are all first guesses (the designer asked to tune the 20 s after play-testing).
- That a quest target ("Quest target:") is really worth a non-silent push; the alternative is silent only.

## Manual in-game checklist
1. Load a save in Goodsprings: one silent greeting that includes "You are currently ..." and one "You are now ..." line after the
   load screen; none during the loading screen.
2. Walk through a door into a building and out: one line each ("You are now in Goodsprings Saloon.", then back outside),
   no line during the load screen, none repeated.
3. Walk away from Goodsprings across wilderness: "near <nearest discovered place>", later "in the wasteland" if nothing
   discovered is within ~300 m; at most one outdoor change per 30 s.
4. Stand still near a container and an NPC: one "You see" summary (silent) within a few seconds, then nothing repeats for the
   same objects; step into view of a quest target: a non-silent summary leading with "Quest target:".
5. Fight, open the Pip-Boy or talk to someone: no "You see" lines until it is over.
6. Log lines to look for: `Location: ...`, `You-see push (...)`. Report if markers never show up in the wasteland.
