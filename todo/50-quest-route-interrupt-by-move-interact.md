# 50 - move_to / interact_with must interrupt the do_current_quest route

Status: open
Area: Walker / Gameplay actions (`src/GameplayHandler.cpp`: `ContinueCurrentQuest`, `StartObjectAction`, `DoCurrentQuest`)
Source: designer's play-testing, 2026-11-09

## Problem
While `do_current_quest` is active, calling `move_to_object` or `interact_with` makes the character walk to that
thing, but then immediately turn back onto the quest route. The route is stored in `g_currentQuestRoute` and
`ContinueCurrentQuest()` restarts the Walker toward the quest target whenever the Walker is idle (or finishes an
`Owner::Object` command), so any other movement command is only a detour that the route undoes. Neuro cannot do
something else (loot a body, talk to a bystander, enter a shop) in the middle of a quest route.

## Decision (designer, 2026-11-09)
Using `move_to` or `interact_with` while the quest route is active **disables the quest route** and interrupts it.
The route stays off until `do_current_quest` is called again. It must not resume on its own after the new command
finishes or fails.

## Work
1. In the code path that starts a Walker command for `Owner::Object` (`StartObjectAction`; check which actions feed
   it: `move_to_object`, `interact_with`, and whatever else uses `Owner::Object`), call `ClearCurrentQuestRoute()`
   before `Walker::Start`, so the route is gone before `ContinueCurrentQuest()` can run on the next frame. Clear it
   only once the new command is accepted (validate and revalidate first; a rejected action must not cancel the
   route). Order matters with the stop in `SelectQuest`, which already stops a Quest-owned Walker command.
2. Decide for the other owners: `explore` (`Owner::Exploration`) probably should interrupt the route the same way;
   combat (`Owner::Combat`) already pauses it because `ContinueCurrentQuest` returns while another owner is active;
   keep that (combat interrupts, then the route resumes) unless the designer says otherwise. Ask if unsure.
3. Tell Neuro about it: add one sentence to the `action/result` or the silent context of the interrupting action,
   e.g. "Your quest route was stopped; call do_current_quest to resume it." Check the `do_current_quest`,
   `move_to_object` and `interact_with` descriptions still agree with this behaviour.
4. Make sure the Walker command currently owned by `Owner::Quest` is stopped (not left running toward the quest
   target) when the new command replaces it, with no held input left behind (see `.claude/rules/combat.md`,
   "release all inputs on every exit path").
5. If the "interrupts the route" rule can be expressed engine-free (which owners clear the route), put it in a small
   helper and add a host test; otherwise note why not.
6. Related: todo 41 (`query_quests` says which quest is selected) should also say whether the route is currently
   running or was interrupted, once this lands (selected quest stays selected; only the route is off).

## Done when
With `do_current_quest` active, `move_to_object` / `interact_with` walks to the new target and stays there; the
character does not turn back onto the quest route; `do_current_quest` starts it again.

## Manual in-game checks
1. Start `do_current_quest`, then `move_to_object` on something off the route. Expected: she walks there, arrives,
   and stays; no jump back to the quest route.
2. Same with `interact_with` (container, door, NPC).
3. After either, call `do_current_quest`. Expected: the route resumes toward the quest target.
4. Send a rejected `move_to_object` (bad id) during the route. Expected: the route keeps running.
5. Start combat during a route, finish it. Expected: unchanged from today (route resumes after combat).
