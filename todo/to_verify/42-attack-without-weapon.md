# 42 - attack with no weapon equipped only looks at the target

Status: to verify
Area: Combat (`CombatHandler.cpp`, `src/Combat/CombatLogic.*`, `AttackAction`)
Source: designer's play-testing, 2026-10-25

## Problem
With nothing equipped, `attack` is accepted but only turns toward the target, while the Walker `do_current_quest`
route keeps going. That looks broken: no shot, no swing, no approach, and no message explaining why.

Likely cause (read before changing): an unarmed engagement is "melee" (`Combat::NeedsReach`), and combat only closes in
through Walker when **no other Walker command is running** (`.claude/rules/combat.md`: melee approach uses
`Walker::Owner::Combat` and never replaces another owner's command). A running quest walk therefore blocks the approach
and fists are never in reach, so the engagement just aims.

## Questions to settle with the designer
- Should `attack` with fists really be allowed during a quest walk, or should it be **rejected up front** ("you have no
  weapon equipped; equip one first, or say you want to punch")?
- If fists are allowed: should the melee approach **take over** a Quest/Object Walker command for the duration of the
  engagement (and give it back after), or should `attack` refuse while another walk is running?
- Does the engagement need a clear message when it cannot reach the target ("you are not in reach and your walk to X is
  still running")?

## Work
1. Reproduce with the log: engagement start, `approach` output, which Walker owner blocked it.
2. Implement the designer's answer in `CombatLogic` (decision) and `CombatHandler` (Walker handoff), with a host test.
3. Whatever the answer, the engagement must not sit silently aiming: it either swings, approaches, or ends with a
   message saying why.

## Done when
`attack` unarmed during a quest walk does one of the agreed things and tells Neuro what happened.

## Result (2026-11-12, through todo 50)
The designer's decision for todo 50 answers the second question: "anything and everything should interrupt
do_current_quest ... attacking etc." So `attack` now ends the quest route and stops whatever walk was running when it
is called (`CombatHandler::StartAttack`). The Walker is then free, the melee approach (`Walker::Owner::Combat`) starts
on the next frame, and fists walk up to the target and swing. Nothing here is specific to fists; it holds for every
melee weapon.

- Fists are still accepted (first question): nothing rejects an unarmed `attack` up front. Say so if it should.
- The third point of "Work" (never sit silently aiming): when combat's walk to the target fails, the engagement now
  ends at once as "could not get to it", with Walker's reason in the same message (todo 55), instead of starting the
  walk over until the 20 s stall. When Neuro starts her own walk during the fight, the engagement still only aims
  while she is out of reach: her walk wins, as before.
- Step 1 (reproduce with the log) was not done: the cause was read from the code, not from a run.

Compiled and host-tested (`tests/CombatTests.cpp`: the melee approach, a failed approach); not run in the game.

## Manual in-game checks
1. Nothing equipped, `do_current_quest` running, `attack` an enemy: she leaves the route, walks up to it and punches.
   The route does not resume afterwards.
2. The same during `move_to_object`: she stops walking there and goes for the enemy.
3. An enemy she cannot get to (on a ledge, behind a locked gate), with fists: the fight ends with "could not get to
   it" and a reason, not after 20 s of standing.
