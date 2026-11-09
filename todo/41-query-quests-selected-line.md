# 41 - query_quests should say which quest is already selected

Status: open
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
