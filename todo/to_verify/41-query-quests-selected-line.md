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

## Update (designer, 2026-11-09)
Seen again in play: `query_quests` gives no hint at all which quest is selected and currently active. Confirmed in
`GameplayHandler::QueryQuests`: it prints only "## Current quest objectives" and one line per catalog entry; neither
`player->activeQuest` nor `g_selectedObjective` is read. Extend the work:
- Besides the top line, **mark the selected objective inside the list** (e.g. append " (selected)" to its entry), since
  the active quest can have several objectives and the top line alone does not say which one `do_current_quest` follows
  (`g_selectedObjective`, falling back to the first objective, see `DoCurrentQuest` preparation).
- Say whether the route is **running or not** ("do_current_quest is following it" / "not following it; call
  do_current_quest"). This matters once todo 50 lands: `move_to_object`/`interact_with` turn the route off while the
  quest stays selected.
- The active quest may have no entry in the list (objective hidden or finished): the top line should still name it.

## Result (2026-11-10, merged)
Two implementations of this todo landed in parallel (one with the "(selected)" tag and route state, one with the
objective in the top line and todo 04's grouped list). The merge keeps the engine-free `src/Context/QuestListing.*`
(namespace `Quests`, todo 04) and carries the other half over onto it.

Built:
- `src/Context/QuestListing.*` (host-tested in `tests/QuestListingTests.cpp`) builds the whole `query_quests` text.
  When a quest is selected the result starts with "You already have quest X selected (current objective: Y). " plus
  either "do_current_quest is following it." or "You are not following it right now; do_current_quest follows it.",
  then a blank line, then the list grouped by quest with `###` headings (todo 04). The quest's heading ends in
  "selected", and the entry `select_quest` chose reads "- `3` (selected) - ...". If its exact target left the list,
  all targets of the chosen objective are tagged; markerless entries are never tagged. A quest selected only in the
  Pip-Boy (no `select_quest`) gets the top line and heading but no entry tag. Nothing selected: no line, no tags.
- `GameplayHandler::ReadSelectedQuest` reads `player->activeQuest` (a completed or failed quest counts as nothing
  selected). The objective in the line is the one picked with `select_quest` while it is still displayed, else the
  quest's only displayed objective; with several displayed and none picked only the quest is named. It also passes what
  `select_quest` chose (`g_selectedObjective`, only for the active quest) and whether the route runs
  (`g_currentQuestRoute` for that quest). The quest is named even when it has no entry in the list.
- The line is only in the query result; quest-update notifications and `BuildFreshStatus` are untouched.
  The `query_quests` description says it reports the selected quest; `select_quest` and `do_current_quest`
  descriptions still agree.

Checks run: `bash tools/test.sh` (all host tests pass), `clang-format`, MSVC Debug build (`just debug`) after the merge.
Not run in game.

Unverified: everything in game, including that `player->activeQuest` matches the quest the Pip-Boy shows as active
after a load, and after the game itself (not Neuro) changes it.

Manual in-game checks:
1. No active quest: `query_quests` starts with "## Current quest objectives"; no "selected" anywhere.
2. `select_quest` one of several objectives, then `query_quests`: the top line names the quest and objective, says the
   route is not being followed; the quest heading and only the chosen entry (or its objective's entries) say selected.
3. `do_current_quest`, then `query_quests` while it walks: the line says it is following it.
4. Select a quest by hand in the Pip-Boy (one with several objectives): the line names the quest without an objective,
   no entry tag; with a single objective it names that objective.
5. Complete or fail the active quest, then `query_quests`: no selected line. Load a save with an active quest: the line
   appears on the first `query_quests`.
