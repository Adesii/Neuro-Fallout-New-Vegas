# 41 - query_quests should say which quest is already selected

Status: to verify
Area: Gameplay actions (`src/Actions/Gameplay/QueryQuestsAction.cpp`, `GameplayHandler::QueryQuests`)
Source: designer's play-testing, 2026-10-25

## Problem
`query_quests` lists objectives and ids for `select_quest`, but never says which quest is the active one. Neuro does
not know a quest is already selected and may select it again or wonder why `do_current_quest` already works.

## Work
1. When a quest is currently selected (`player->activeQuest` / the current quest route in `GameplayHandler`), put one
   extra line at the **top** of the `query_quests` result: "You already have quest X selected." (name it the way the
   list names quests; mention the objective if it is cheap to get).
2. Nothing selected: no extra line.
3. The line only appears in the query result; do not add it to the quest-update notifications.
4. Check `select_quest` / `do_current_quest` descriptions still agree with the new wording.
5. If the text is assembled in an engine-free helper, add a host test for with/without a selection.

## Done when
`query_quests` starts with the selected-quest line when one exists, and the list below it is unchanged.

## Result (2026-11-10)

Built:
- `src/Context/QuestListing.*` (engine-free, `tests/QuestListingTests.cpp`) builds the whole `query_quests` text. When a
  quest is selected the result starts with "You already have quest X selected (current objective: Y)." then a blank
  line, then the old "## Current quest objectives" list, unchanged. With nothing selected the text is exactly as before.
- `GameplayHandler::QueryQuests` reads `player->activeQuest` (`ReadSelectedQuest`). A completed or failed active quest
  counts as nothing selected. The objective is the one picked with `select_quest` while it is still displayed, else the
  quest's only displayed objective. With several displayed and none picked, only the quest is named (no guessing).
  The quest name comes from the same `QuestName` the list uses.
- The line is only in the query result; quest-update notifications and `BuildFreshStatus` are untouched.
- `query_quests` description now also says it reports the selected quest. `select_quest` ("does not start travel") and
  `do_current_quest` ("Follow the currently selected quest objective") still agree with the wording.

Checks run: `bash tools/test.sh` (all host tests pass), `clang-format`. Not compiled with MSVC in Cloud (no toolchain
installed), so `just debug` still needs to run locally.

Unverified: everything in game, including that `player->activeQuest` matches the quest the Pip-Boy shows as active
after a load, and after the game itself (not Neuro) changes it.

Manual in-game checks:
1. New game / save with no active quest: `query_quests` starts with "## Current quest objectives" as before.
2. `select_quest` on an objective, then `query_quests`: the first line names that quest and objective; the list below is
   unchanged.
3. Select a quest by hand in the Pip-Boy (one with several objectives), then `query_quests`: the line names the quest
   without an objective; with a single objective it names that objective.
4. Complete or fail the active quest, then `query_quests`: no selected line.
5. Load a save with an active quest: the line appears on the first `query_quests`.

Later change (2026-11-10, todo 04): the list below the line is now grouped by quest with `###` headings, so "the list
below it is unchanged" no longer holds; check item 2 against the grouped format.
