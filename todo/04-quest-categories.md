# 04 - Quest categories: main / DLC / optional

Status: in progress (grouped listing done, to verify; categories still open)

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

Still open from this task: the category tags (main / optional / DLC) and the main-first ordering from the decision
above; the quest heading is where a category tag would go.

Unverified in game: that markerless objectives really are the displayed objectives `BuildQuestCatalog` skipped (and
not hidden or stale ones), and how the headings read for Neuro.

Manual in-game checks:
1. `query_quests` with two or more active quests: each has its own heading, ids are 1..n top to bottom, and
   `select_quest` with an id picks the line it is printed on.
2. An objective with several markers (e.g. a "kill" or "collect" objective): numbered places, each id selectable.
3. An objective without a map marker: listed under its quest without an id, and the Pip-Boy shows the same objective.
4. The selected quest's heading carries "(selected)".
