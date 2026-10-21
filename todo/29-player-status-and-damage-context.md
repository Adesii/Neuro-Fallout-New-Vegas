# 29 - Damage, death and combat-start messages

Status: open
Area: Context / Combat (`CombatHandler.cpp`, `src/Combat/CombatLogic.*`)
Source: `docs/code-review.md` A7.3, A7.7, B9 (FlushNotes), B10

## Problem
- Health is reported only as band crossings and only inside a combat context (`CombatLogic.cpp:322-364`, flushed at most
  every 3 s). No death message, no out-of-combat damage, no attacker names.
- Skyrim: "X hits you with W!" silent, once per attacker per 10 s (`Observer.hpp:154-177`), drowning and death
  narration (`Observer.cpp:8411,9146`), "[YOU DIED...]" plus a deliberately non-silent advice line after death
  (`Observer.cpp:9218`).
- The combat-start message is a long non-silent block (hostile list, status, usage text; `CombatHandler.cpp:555-570`).
- `FlushNotes` clears the notes and starts the cooldown before knowing whether the send succeeded
  (`CombatHandler.cpp:519-529`).

## Decisions (designer, 2026-10-21)
- Add a **death message** (non-silent): a short fact + what happens next (reload from save) **plus why she died and a
  short lesson hint**. The designer wants Neuro to know exactly what killed her (and what she was doing) so she can
  talk about it on stream and react to it.
- Add **per-attacker hit notes** (silent, at most one per attacker per 10 s).
- Add **non-combat damage notes** (falls, radiation ticks, traps/explosions) as banded health changes outside combat.
- Combat start: **trim the usage paragraph**, keep hostile ids and a short status (the ids save a query round trip).

## Work
0. Cause of death: remember the last damaging attacker/cause while alive (hit notes, step 2), and read the actor's
   killer field as a candidate (`Actor::killer`, JG `GameObjects.h` offset `0xC0`, verify it is the Actor struct used here
   and that it is set for player death; no invented hooks). Cover: killed by an actor (name + weapon if known), radiation,
   fall, explosion/trap, drowning, unknown. Text template: fact -> cause -> one short lesson sentence chosen from the
   cause (e.g. "You were at 12% health when you charged in; check your health before fighting"). Keep the lesson
   stateless like Skyrim's advice line (`Observer.cpp:9218`).
1. Death: detect the player's death (already known as `StopReason::PlayerDied` for engagements); send a non-silent
   message saying what happened and that the game will reload from a save (mirrors what `plugin.cpp` does on
   `PreLoadGame`). Do not rely on state that is reset by the reload message ordering; verify the order in game.
2. Hit notes: find a verified source for "who damaged the player" (do not invent hooks; check
   `docs/reference-research.md`, JIP-LN-NVSE/yUI event hooks such as the NVSE `OnHit`-style events). If no reliable
   source exists, fall back to health-delta + `Actor::combatTarget` of nearby hostiles, labelled best-effort.
3. Rate limiting per attacker and the out-of-combat banding live in `CombatLogic` (engine-free, host-tested).
4. Combat start: remove the "Use the ids above..." paragraph from `StartCombatContext`; keep
   `BuildPlayerStatus()` and the hostile list.
5. `FlushNotes`: clear notes/cooldown only after `SendContext` returned true.
6. Reconsider ownership: a `PlayerStatus` reporter that runs outside combat as well belongs with the combat reporter,
   not in a second module (`.claude/rules/combat.md`).

## Existing precedent
- `docs/combat.md` design section: combat context is event-driven, non-silent, batched at most every 3 s; keep that
  for anything that happens during an engagement.

## Open questions
- Draft the cause templates and show them before coding (designer: mostly neutral coaching, sometimes a playful roast).

## Answered (designer, 2026-10-21)
- Lesson tone: **mix** - neutral coaching most of the time, a playful roast now and then. Keep the roast choice
  deterministic enough to test (seeded RNG in the engine-free module, roast about 1 in 4 deaths, never about
  something she could not have known).
- Hit notes: **batch** them with the existing combat update (3 s), do not send separately.

## Done when
Dying, falling, standing in radiation and being shot outside a recognised combat all produce one clear message each,
notes are never lost on a failed send, combat start is shorter, tests cover the new reporter logic, and the checklist
in `docs/combat.md` is extended.
