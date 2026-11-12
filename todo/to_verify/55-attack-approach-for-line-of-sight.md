# 55 - attack can walk toward a target that is not in view

Status: to verify
Area: Combat (`src/Combat/CombatLogic.*`, `src/CombatHandler.cpp`, `src/Actions/Combat/AttackAction.cpp`)
Source: designer's play-testing, 2026-11-12
Related: `to_verify/50-quest-route-interrupt-by-move-interact.md` (attack stops the walk that was running),
`to_verify/42-attack-without-weapon.md`, `47-auto-aim-down-sights.md`

## Problem
`attack` is refused when the target is not in the line of sight, and an engagement whose target goes behind cover
only waits 8 s and then gives up. Neuro has to work out by herself where to stand. Designer: "there should either be
a parameter or another action that also allows to move to the target until it's in LOS and attacking then, that way
attacking an enemy behind cover tries to move into a position to get LOS."

## Decision
A parameter on `attack`, not a second action: `approach` (boolean, optional, default false). One verb with one more
choice, as the neuro-sdk guidance prefers. The designer left the choice between the two open; the default (off) is my
reading of "also allows", see "To confirm".

## Result (2026-11-12)
Built, host-tested and compiled; not run in the game.

- `attack {enemy_id | object_id, approach?}`. `Combat::ParseAttackRequest` accepts the optional boolean
  (`ActionData::ReadBoolean` is new: a JSON `true` or `false`, nothing coerced).
- With `approach: true`, `attack` is accepted without a line of sight. Without it the refusal now says how: "Call
  attack again with approach set to true to walk toward it until you have a clear shot, or move to a clear view
  yourself first."
- The engagement (`Combat::Engagement`, begun with `seekSight`) asks for a walk toward the target whenever it has no
  line of sight, at the start or when the target goes behind cover later. `CombatHandler` runs that walk through the
  Walker as `Owner::Combat`, the same way as the melee approach, so it uses the navmesh route, the ray check and the
  grid.
- When the target comes into view she fires at once and keeps walking for 0.3 s more (`Engagement::kSightHold`), so
  she ends up clear of the corner. A view that flickers at the edge of cover does not stop and restart the walk.
- The 8 s line-of-sight limit does not run while that walk is covering ground (20 units per second, measured in 1 s
  windows; any direction counts, because the way round cover often does not shorten the straight line). It runs again
  when she stands still without a view: she arrived and still cannot see it, or she is walking somewhere else on
  Neuro's own command. The engagement then ends with "you still have no clear view of it, and walking toward it did
  not open one".
- When the Walker gives up on the walk (no path, blocked, stuck), the engagement ends at once as
  `StopReason::Unreachable`: "You stopped attacking X because you could not get to it." followed by Walker's own
  reason, in one message. This also covers the melee approach, which used to start the same failing walk again until
  the 20 s stall, with a failure message each time.
- A walk that ends by itself next to a target that is still out of view is started again at most twice a second.
- Neuro's own walk command during the fight still wins over the approach (`externalMovement`), and a menu pauses it.
- The "Attacking" message says "It is not in view, so you are walking toward it until you have a clear shot" or "If
  it goes behind cover you will walk toward it until you can see it again". The action description names `approach`.

Host tests: `tests/CombatTests.cpp` ("combat approach: ..." and the `approach` argument), `tests/ActionDataTests.cpp`.

## To confirm (designer)
- **Default off.** `attack` without `approach` behaves as before, and Neuro has to ask for the walk. If the walk
  should be the default (and `approach: false` the way to hold position), it is a small change: the default in
  `Combat::AttackRequest` and two sentences of text.
- **She walks toward the target itself.** The route is the Walker's route to the enemy, stopped as soon as she can
  see it. It does not look for a flanking spot or for cover of her own; against a melee enemy that means walking into
  it. Say so if a smarter position is wanted.

## Unverified
- That walking along the navmesh route toward a target opens a line of sight soon enough to be useful. Around a
  rock or a corner it should. A target on a roof or a ledge can end as "no clear view" or "could not get to it".
- The 0.3 s hold, the 20 units per second and the twice-a-second restart are guesses.
- Line of sight is the ray to the chest point (todo 56). A target seen only through a gap at head height counts as
  out of view.
- Walking while combat owns the aim strafes; whether the walk looks right while the view is locked on the target.

## Manual in-game checks
1. An enemy behind a rock or a wall, `attack` with only `enemy_id`: refused, and the message names `approach`.
2. The same with `approach: true`: accepted. She walks toward it, starts shooting when it comes into view, takes a
   step or two more and stops walking. The log has `Combat engagement started ... [approach] [not in view]`.
3. During that fight the enemy runs behind cover: she walks after it and shoots again when she sees it. She does not
   stand still for 8 s and give up.
4. `attack` with `approach: true` on an enemy she can already see: she shoots from where she stands and does not
   walk.
5. An enemy she cannot reach or see (on a roof, behind a locked gate): the fight ends within about ten seconds with
   one message, either "could not get to it" with a reason or "no clear view", not a repeating failure.
6. During an approach, `move_to_object` somewhere else: she goes there instead, and the fight ends after about 8 s
   without a view.
7. Open the Pip-Boy during an approach and close it: the walk continues. `stop_attack` during an approach: she stops
   walking and no key stays held.
8. A melee weapon and an enemy round a corner, with and without `approach`: with it she keeps going until she is in
   reach; without it the fight ends after 8 s out of view, as before.
9. A bottle behind a post, `attack` with `object_id` and `approach: true`: she walks until she sees it and fires the
   burst.
