# 50 - Anything Neuro starts herself interrupts the do_current_quest route

Status: to verify
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

## Update (designer, 2026-11-12)
Seen again in play: only `stop_move` gets her off the quest route. The designer widened the decision: "anything and
everything should interrupt do_current_quest, like walking somewhere manually, interacting with anything, attacking
etc." This replaces step 2's "combat pauses the route and it resumes afterwards".

## Result (2026-11-12)
Built, host-tested and compiled; not run in the game.

- The rule is one engine-free table, `WalkerLogic::EndsQuestRoute(Command)` (`src/Movement/WalkerLogic.*`, tests in
  `tests/WalkerLogicTests.cpp`). These end the route: `move_to_object`, `interact_with_object`, `explore`, `attack`,
  `open_vats`. These do not: the queries, `crouch_toggle`, `open_inventory`.
- `GameplayHandler::InterruptQuestRoute(command)` clears the route, stops a walk the route owns (`Walker::Stop`
  releases every held input) and sends one silent note: "You stopped following <quest: objective> to do something
  else. The quest stays selected; call do_current_quest when you want to follow it again." It is called from
  `StartObjectAction`, `StartExploration`, `CombatHandler::StartAttack` and the VATS opener, each only after the
  action was validated, revalidated and accepted, so a rejected action leaves the route running.
- The route does not come back by itself: nothing restarts it except `do_current_quest`.
- `attack` also stops any other walk that was running when it was called (a `move_to_object`, an `explore`), and
  says so in its "Attacking" message. That is my reading of "attack is the newest command, so it wins"; the designer
  only asked for it against the quest route. It also frees the Walker for combat's own approach, which is what todo
  42 was missing. A walk started after `attack` still moves her without ending the fight, as before.
- `do_current_quest`'s description now says that anything else she starts ends the route. `query_quests` already
  says "You are not following it right now; do_current_quest follows it" when the route is off (todo 41).

My choices, to confirm:
- Menus do not end the route. Opening the inventory, a container, a dialogue or a terminal only pauses the walk, and
  the route carries on when the menu closes. The route itself opens doors, dialogue and containers on the way, so a
  menu choice ending it would break the route's own steps.
- Queries and `crouch_toggle` do not end it either: looking around or sneaking is not doing something else.
- `open_vats` ends it like `attack`, also when VATS is then cancelled without a shot.

Unverified:
- That nothing is left held when the route's walk is stopped mid-stride (it goes through the same `Walker::Stop` as
  `stop_move`).

## Manual in-game checks (replace the list above)
1. `do_current_quest`, then `move_to_object` on something off the route: she walks there and stays. The log has
   `Quest route interrupted by another command`, and Neuro gets the "You stopped following ..." note once.
2. The same with `interact_with_object` (a container, a door, an NPC) and with `explore`.
3. `do_current_quest`, then `attack` an enemy: she stops walking the route, fights, and stands still afterwards. The
   route does not resume when the enemy is dead.
4. `do_current_quest`, then `open_vats`: the route is off after VATS, with or without a shot.
5. After any of these, `do_current_quest` again: the route resumes toward the quest target.
6. A rejected action during the route (`move_to_object` with a bad id, `attack` on a dead enemy): the route keeps
   running.
7. During the route: `query_nearby`, `query_enemies`, `crouch_toggle`, `open_inventory` and closing it again. The
   route keeps running each time.
8. A route step that opens a door or starts a dialogue: choosing a dialogue line does not end the route.
9. `move_to_object` somewhere (no quest route), then `attack`: she stops walking and the "Attacking" message says so.
