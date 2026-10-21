# 26 - Walker: interaction checks and feedback to Neuro

Status: open
Area: Walker + `GameplayHandler::ProcessWalkerEvents`
Source: `docs/code-review.md` A2, A4, D (Combat `Started` events)

## Problem
- `TryComplete` activates once the 3D distance is under 100 (`WalkerHandler.cpp:451,482`); no line-of-sight or
  crosshair check, so it can activate through a floor or railing. `Intent::Move` can report "Reached" on another floor.
- Messages: started/completed/failed only. The failure text is generic ("Could not reach X.", `:550`).
- Long walks produce nothing after the silent "Started". Skyrim sends a non-silent reminder every 40 s (20 s while
  exploring): distance walked, walk time, distance to target (`WalkerProcessor.cpp:3975-4105`), and specific failure
  text with the blocking object when known.
- `Walker::Start` pushes a `Started` event for `Owner::Combat` every approach and `ProcessWalkerEvents` throws it away
  (`WalkerHandler.cpp:509`, `GameplayHandler.cpp:471`).

## Decisions (designer, 2026-10-21)
- Interaction: **prefer line of sight / reticle on the target, but keep today's give-up**: after the alignment timeout
  (`kAlignmentGiveUpDelay`, 5 s) it still activates and lets the game accept or reject.
- Progress reminder **every ~40 s, non-silent**, content as above; excluded for `Owner::Combat`. Shorter interval
  (~20 s) while exploring.
- Failure messages must carry a **reason** (stuck, door locked, target on another floor, target gone, game rejected the
  activation).

## Work
1. Share a line-of-sight helper: move `HasLineOfSight`/`RayBlocked` from `CombatHandler.cpp:212-247` into
   `utils/` (e.g. `WorldUtils`) so Walker and Combat use one implementation. `docs/combat.md` unverified item 1 (the
   ray offsets) applies to both.
2. Add the check to `TryComplete`; when the ray is blocked for the whole alignment timeout log it and continue to
   interact (decision above). Add a vertical mismatch check to `Intent::Move` completion.
3. Walker reports a `FailureReason` enum with each failed event; `GameplayHandler` builds the text. Keep the
   quest door-step wording (`GameplayHandler.cpp:486-495`, a good Skyrim-beating detail).
4. Reminder timer: engine-free, host-tested (same style as `Combat::CombatReporter`); sent through the existing
   `ProcessWalkerEvents` path so Walker does not talk to `NeuroSDK` directly.
5. Do not generate `Started` events for `Owner::Combat`.

## Existing precedent
- `CombatHandler` already solves LOS (`.claude/rules/combat.md`, `docs/combat.md` Evidence section).
- The poke system (todo 24) should treat a recent reminder as activity so it does not double up.

## Open questions
- (Answered) Distances are reported in metres, see `39-distances-in-metres.md`; use its formatter for the reminder.

## Done when
Interacting through geometry no longer happens in the common cases, reminders arrive during a long walk, failure
messages name a cause, and the checklist covers: walk behind a railing, a 2-minute walk, a stuck failure.
