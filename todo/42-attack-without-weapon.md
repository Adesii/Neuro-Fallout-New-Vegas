# 42 - attack with no weapon equipped only looks at the target

Status: open
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
