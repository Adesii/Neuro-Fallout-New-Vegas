# 53 - Custom (plugin-defined) quests

Status: open
Area: Quests (`src/GameplayHandler.cpp` quest catalog; new engine-free registry beside `src/Context/QuestListing.*`)
Source: designer, 2026-11-09 ("I'll surely add similar quests later")
Related: `to_verify/41-query-quests-selected-line.md`, `04-quest-categories.md`, `52-quest-description-on-first-follow.md`

## Idea (from the Skyrim integration, idea source only)
Skyrim's `get_current_quests` (`Misc.cpp` around line 22819) adds **fake quests** that do not exist in the game's quest
data, marked `phantom_objective` with the player as the phantom target and category "main". Example: "Find lockpicks"
(when she has none) and "Get some gold" (when low), each with a short hint text in brackets ("Lockpicks are usually
sold by traders..."). They appear in the list with an id like real quests, vanish once the condition no longer holds,
and give her something to do when no real quest fits her state.

## Goal
A small data-driven way to define custom quests in the plugin, so the designer can add more later without touching
the quest catalog code each time.

## Design sketch (to settle with the designer)
- A custom quest has: a stable key, a name, an objective text, a hint text, a category (todo 04), an **active
  condition** (when it shows), an optional **completion condition**, and an optional **target** (a reference, a
  location, or none = phantom "just do this" objective).
- Registry and condition evaluation live in an engine-free module with host tests; only the condition inputs (player
  gold, item counts, location, quest stages, ...) are read from the engine by `GameplayHandler` and handed in as plain
  values, like the other Context helpers.
- `BuildQuestCatalog` appends the active custom quests to the real ones; `query_quests` lists them with ids and
  the "(selected)" tag like the rest; `select_quest` accepts them.
- `do_current_quest` on a custom quest **without a target** cannot walk anywhere: it answers with the hint text
  ("Nothing to walk to for this. <hint>") instead of failing mysteriously. With a target it uses the normal Walker route.
- Completion: send "Quest completed: X" through the same notification path as real quests, once, and drop it.
- Keep it separate from real quests in logs and in `g_selectedObjective`/route bookkeeping (it must not touch
  `player->activeQuest`; `ActivateQuest` only works for real `TESQuest` forms).

## Open questions
- **Real ESM quests instead?** Quests added to `esms/Neuro_FNV.esm` through the GECK would appear in the engine's own
  objective list with no C++ at all. But no checked-in command regenerates the ESM from `scripts/`, and the ESM is
  binary, so the designer must say how those quests are authored. A C++ registry covers cases the ESM cannot
  (conditions on plugin state), and both can coexist. Verify how ESM quests show up in `questObjectiveList` before relying on it.
- Which custom quests are wanted first (the Skyrim examples are lockpicks and gold; FNV equivalents might be caps,
  ammo, healing items, a place to rest)?
- Should custom quests respect `selected`/route state exactly like real ones (todo 41 wording) or be listed separately?

## Done when
At least one custom quest defined through the registry shows in `query_quests`, can be selected, answers
`do_current_quest` sensibly with and without a target, and disappears/completes when its condition says so; adding
another needs only a registry entry (plus host test).

## Manual in-game checks
1. Meet the active condition: the quest appears with a fresh id; leave it: it disappears.
2. `select_quest` it, then `do_current_quest`: hint text (phantom) or a normal walk (with target).
3. Complete it: one completion notification, no repeats.
4. Real quests keep working next to it; the ids stay consecutive in one list.
