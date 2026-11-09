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

## Result (2026-11-09)
- New engine-free `src/Context/QuestListing.*` (host-tested in `tests/QuestListingTests.cpp`): `Format` builds the
  `query_quests` text, `MarkSelected` decides which entries get "(selected)".
- `GameplayHandler::QueryQuests` feeds it: the active quest (`player->activeQuest`, ignored when completed/failed),
  what `select_quest` chose (`g_selectedObjective`) and whether the route runs (`g_currentQuestRoute` for that quest).
- Output: under `## Current quest objectives` a first line "You already have quest X selected. do_current_quest is
  following it." / "You are not following it right now; do_current_quest follows it."; the chosen entry reads
  "- `3` (selected) - ...". If its exact target left the list, all targets of the chosen objective are tagged. A quest
  selected only in the Pip-Boy (no `select_quest`) gets the top line but no entry tag. Nothing selected: output unchanged.
- The `query_quests` description now mentions it. `select_quest` / `do_current_quest` descriptions still agree.
- Quest-update notifications are untouched.
- Host tests and the MSVC Debug build pass. Not run in game.

### Manual in-game checks
1. No quest selected: `query_quests` has no extra line and no "(selected)".
2. `select_quest` one of several objectives, then `query_quests`: the top line names the quest and only that entry
   (or its objective's entries) is tagged; the line says the route is not being followed.
3. `do_current_quest`, then `query_quests` while it walks: the line says it is following it.
4. Select a quest in the Pip-Boy by hand: top line, no entry tag. Complete the quest: the line disappears.
