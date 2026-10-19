# 07 - query_nearby: top 20 with priority

Status: open

## Problem
`query_nearby` fills up with junk (unusable or worthless objects) while more valuable things slightly further away
are cut off.

## Goal
Return at most 20 entries, chosen by importance first and distance second, so valuable/usable objects beat clutter.

## Notes
- Code: `src/Actions/Gameplay/QueryNearbyAction.*`, `src/GameplayHandler.*`, `WorldUtils::ForEachLoadedReference`.
- Need a priority scheme; propose one and confirm with the designer. Candidates: quest targets and NPCs first, then
  containers, doors, workbenches/terminals, then valuable pickable items (by value/weight or item type), then plain
  scenery/junk last (or dropped). Items that cannot be activated or taken should not take slots.
- Keep the scoring in an engine-free module with host tests (like `src/Combat/CombatLogic.*` / `src/Inventory/`), fed
  by observations from the engine glue.
- Say in the result when entries were truncated (e.g. "showing 20 of N, most important first") so she knows to move or
  query again; keep ids stable so `object_id` still works with `attack` and `move_to_object`.
- Don't hide explicit targets she asked for; `attack` with `object_id` for a bottle must still be possible (bottles may
  be junk by value but are quest-relevant - handle quest-relevant objects explicitly).

## Done when
Goodsprings queries show important things first with a cap of 20, scoring is covered by host tests, in-game checklist given.
