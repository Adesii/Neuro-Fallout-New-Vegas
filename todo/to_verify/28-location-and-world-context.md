# 28 - Location notices and throttled "you see" push

Status: to verify
Area: Context (new module, fed by engine reads)
Source: `docs/code-review.md` A7.1, A7.5

## Problem
- FNV never tells Neuro where she is. Skyrim's `[Current location: X ...]` (`main.cpp:1861`) is its main "where am I"
  cue; if the walker is active it is sent immediately, otherwise queued and merged.
- FNV is pull-only for surroundings (`query_nearby`). Skyrim pushes "You see: ..." notices
  (`Observer.cpp:3599-4036`), choosing silent vs non-silent per object type.

## Decisions (designer, 2026-10-21)
- **Location**: one-line, **non-silent** "You are now in <place>" when the cell/worldspace changes, sent after loading
  finishes. No advice text.
- **"You see" push**: a **throttled** push for only the most notable new things (quest targets, containers, NPCs
  entering range): at most one short summary every N seconds, deduped by form id, non-silent only for quest targets.
  Not a full Skyrim-style per-object stream.

## Work
1. Location observer: compare `TES::currentInterior`/worldspace/`cell->fullName` between frames (reuse
   `GameplayHandler::PlaceName` logic); debounce across load screens. Naming rule (designer): use the cell's own name
   when it has one; otherwise **"near <nearest city/village/settlement name>"**; if nothing named is nearby, fall back to a
   generic **"the wasteland"** (or similar). The nearest-named-place lookup needs research first (map markers /
   `TESWorldSpace` map data / location refs; verify against `docs/reference-research.md` sources, do not invent) and a
   sensible max distance, expressed through the metres formatter of todo 39. Engine-free part: choosing among
   candidates by distance + the fallback text, host-tested.
   Engine-free "has the place changed / is it worth sending" in a host-tested module.
2. The location line becomes part of the greeting (todo 24) and of the post-reconnect status (todo 30).
3. "You see" push: define "notable" together with `todo/07` (priority scoring is shared, do not write two scorers).
   Needs: per-form-id memory (cleared on cell change / load), a global rate limit, suppression while combat, menus,
   dialogue or an active force.
4. Write `docs/design/location-and-see-push.md` (skill `skyrim-feature-port`) before coding.

## Existing precedent
- Quest notifications already diff snapshots every 500 ms (`GameplayHandler.cpp:408-460`); copy that style
  (previous snapshot, no streaming of small changes).
- `todo/06`/`07` for scoring and busy-state reminders.

## Answered (designer, 2026-10-21)
- Pushed "you see" text carries **no ids**: ids could go out of sync before she uses them; she calls `query_nearby` to act.
- Start with a 20 s minimum interval between pushes; tune after play-testing.
- Location naming: see work item 1 (cell name, else "near <settlement>", else "the wasteland").

## Open questions
- Maximum distance for "near <settlement>" (suggest ~300 m / tune), and which marker types count as a core location
  (cities, villages, major landmarks, camps?). Decide after the research step shows what data exists.

Distances in any text here follow `39-distances-in-metres.md`.

## Result (2026-10-24)
Built (design note: `docs/design/location-and-see-push.md`):
- Location: `src/Context/Location.*` (describer + tracker, `tests/LocationTests.cpp`), read by `GameplayHandler::ObservePlace`.
  Cell name, else "near <nearest visible map marker within ~300 m>", else "in the wasteland". Non-silent "You are now ..." line
  once a place stayed changed for 1.5 s; the greeting and the reconnect status carry the current place.
- "You see": `src/Context/SeePush.*` (`tests/SeePushTests.cpp`) fed by `GameplayHandler::ObserveSurroundings`: quest targets,
  containers and living actors within ~21 m, at most one summary per 20 s, 5 entries, each form id once per space, no ids,
  non-silent only for quest targets. It reuses the scorer from todo 07/35 (`Objects::Tier`).
- Map markers: base form 0x10 + `ExtraMapMarker` (JG `MapMarkerData.hpp`, JIP-LN-NVSE) with the Visible flag, from the grid
  cells and the worldspace's persistent cell. All marker types count; the maximum distance is 300 m (21000 units).

Unverified (in game): where the markers live, that Visible means discovered, how wilderness cell names look, every threshold.
Manual checks: the checklist in the design note.

## Done when
Walking through a door and loading a save both produce one correct location line; the push stays quiet in idle
cells, fires for a quest target / container entering range, and never repeats the same form id.
