# 04 - Quest categories: main / DLC / optional

Status: in progress (grouped listing and DLC/mod tags done, to verify; main vs side needs in-game data)

## Problem
`query_quests` and `select_quest` list every quest alike, but quests differ: main story, DLC, and optional/side
quests. Neuro cannot tell what is a main objective.

## Goal
Group (or label) quests by category and make sure Neuro can choose by category or at least see it.

## Notes
- Code: `src/Actions/Gameplay/QueryQuestsAction.*`, `SelectQuestAction.*`, `DoCurrentQuestAction.*`, and the Pip-Boy
  quest handling in `src/Menus/PipBoyHandler.*`.
- Open question to answer first: what in the data distinguishes them? Candidates: the quest form's mod index (DLC
  plugin load-order prefix / master name), the quest flags (e.g. "Start Game Enabled", priority), the Pip-Boy quest
  list order, GECK quest "Priority"/"Event". Do NOT guess flags: check reference repos and `docs/reference-research.md`,
  and the private menu extracts for the Pip-Boy quest tab, and verify in game.
- DLC detection by master file is the most reliable; main vs optional probably needs a quest-priority or name-based
  rule. Ask the designer how Neuro should prefer them (e.g. main first, DLC marked, optional last).
- Keep ordering stable between calls; test the grouping/ordering logic on the host if it is engine-free.

## Decision (designer, 2026-10-21)
Ordering: **main story first, then optional/side quests, DLC quests labelled** with their DLC name but sorted by their
own priority. Neuro can still pick any quest. Every line in `query_quests` carries a category tag. The data source for
main vs optional is still the research question above.

## Done when
Quest lists show a category, ordering follows the designer's preference, and the checklist covers a DLC and a side quest.

## Grouped listing (added 2026-11-10, implemented, to verify in game)

Folded in from a review of `query_quests` after todo 41: the list was flat ("Quest: objective" on every line), an
objective with several targets showed up as several identical-looking lines, and displayed objectives without any
usable target (no map marker) were missing entirely.

Built (`src/Context/QuestListing.*`, host-tested in `tests/QuestListingTests.cpp`; `GameplayHandler::QueryQuests`):
- One `### <quest>` heading per quest, its objectives below it. The selected quest's heading says "(selected)" (the
  todo 41 line at the top stays).
- An objective with several targets: one line per target, "(place k of n)", each with its own id and location.
- A displayed objective with no usable target is listed under its quest without an id: "... - no map marker, so it
  has no id to select or follow".
- Order is unchanged (quest priority, then quest, objective, target), now shared by the catalog sort and the listing
  (`Quests::ListedBefore`), so ids still read top to bottom and `select_quest` ids mean the same entries.

Category tags: see "Categories" below.

Unverified in game: that markerless objectives really are the displayed objectives `BuildQuestCatalog` skipped (and
not hidden or stale ones), and how the headings read for Neuro.

Manual in-game checks:
1. `query_quests` with two or more active quests: each has its own heading, ids are 1..n top to bottom, and
   `select_quest` with an id picks the line it is printed on.
2. An objective with several markers (e.g. a "kill" or "collect" objective): numbered places, each id selectable.
3. An objective without a map marker: listed under its quest without an id, and the Pip-Boy shows the same objective.
4. The selected quest's heading carries "(selected)".

## Categories (2026-11-10, research and partial implementation)

### Research: what in the data distinguishes them
- **No main/side field.** `TESQuest` (FNV 1.4.0.525) holds flags, priority, delay, stages, objectives/variables,
  conditions, script locals, current stage and the editor ID, nothing else. Three definitions agree: vendored
  `libs/nvse/JG/nvse/GameForms.h` (`TESQuest`, +0x3C flags, +0x3D priority, +0x64 `editorName`), JIP-LN-NVSE
  `nvse/GameForms.h` (`TESQuest`, same layout, flag enum) and JohnnyGuitarNVSE `JG/internal/Game/Bethesda/TESQuest.hpp`
  (`_QuestFlags`: enabled, completed, allow repeated topics/stages, starts enabled, displayed in HUD, failed; and
  "GetEditorName ... works in vanilla"). None of the flags says "main story". Skyrim's quest type does not exist here.
- **DLC: by defining plugin.** A form id's top byte is the load-order index of the file that defines it. The file is
  `TESDataHandler::pFileArray[index]` with `uiCompiledFileCount` as bound (JG `TESDataHandler.hpp`, singleton
  `0x11C3F2C`), `TESFile::GetName()` the file name. JIP `GameData.h` reads the same array as
  `ModList::loadedMods` in `GetNthModName`, also from `*(DataHandler**)0x11C3F2C`. (JG's offset comments for
  `uiCompiledFileCount`/`pFileArray` say 0x214/0x218, but with an 8-byte `BSSimpleList` at 0x210 the compiled offsets
  are 0x218/0x21C, which is what JIP's `ModList` (`loadedModCount` +0x08, `loadedMods` +0x0C at 0x210) says.)
- **Main vs side: no source in the corpus.** No reference repository lists FNV main-quest editor IDs, form ids or
  DLC plugin file names. The public wikis (fallout.fandom.com, geckwiki.com) are blocked by this Cloud session's
  network policy, so the main-quest list could not be sourced and was not filled from memory. Quest priority is not
  known to separate them either (no source).
- **Pip-Boy:** FalloutNVAccess `src/menus/MapMenuHandler.cpp` reads the quest list flat (name, `_selected` trait,
  flags); nothing in it groups quests by category.

### Built (engine-free part host-tested, engine part compiled by nobody yet)
- `Quests::Classify(pluginFile, editorId, mainEditorIds)` in `src/Context/QuestListing.*`:
  - base game file (`FalloutNV.esm`, any case) or unknown file: "main story" / "side quest" by the main list; while
    `MainQuestEditorIds()` is **empty** (it is), every base-game quest is tagged "base game" instead, so nothing is
    wrongly called a side quest;
  - known add-on masters: "DLC: Dead Money", "DLC: Honest Hearts", "DLC: Old World Blues", "DLC: Lonesome Road",
    "DLC: Gun Runners' Arsenal", "DLC: Courier's Stash" (Caravan/Classic/Mercenary/Tribal packs). **The file names
    are the commonly known ones, not sourced**; a wrong name only falls through to the next rule;
  - any other plugin: "mod: <file name without extension>".
- Ordering: `Quests::CategoryRank` puts main story first (rank 0), everything else after it with its own priority
  (designer's decision: DLC not pushed below side quests). Catalog ids and the listing share the order.
- The quest heading carries the tag: `### Ain't That a Kick (base game, selected)`, `### Bitter Springs (DLC: Honest
  Hearts)`.
- `GameplayHandler`: `QuestPluginFile` (the data handler read above), `QuestEditorId` (`editorName`), and one
  `_DMESSAGE` per quest per `query_quests`:
  `Quest <formid> "<name>" editor=<id> plugin=<file> priority=<n> category=<tag>` to collect the real data.

### Left (needs the game)
1. Build with `just debug` (not compiled in Cloud: no MSVC).
2. In game, with the DLCs installed, run `query_quests` at several points of the main story and read the
   `Quest ...` log lines: check the plugin names (fix `kKnownAddOns` if one differs) and that base-game quests say
   `plugin=FalloutNV.esm`.
3. From those lines (or the GECK), collect the editor IDs of the base-game main quests (Ain't That a Kick, They Went
   That-a-Way, Ring-a-Ding-Ding!, the four endgame lines) and put them in `kMainQuestEditorIds`
   (`src/Context/QuestListing.cpp`); then the tags become "main story" / "side quest" and main story sorts first.
   Decide with the designer whether faction quests that only lead into an endgame line count as main.
4. Manual checks after that: a main quest is listed first with "main story", a side quest says "side quest", a DLC
   quest "DLC: <name>" and sits in priority order with the side quests, a quest from a mod "mod: <file>", and
   `select_quest` ids still match the printed lines.
