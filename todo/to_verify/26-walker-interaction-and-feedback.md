# 26 - Walker: interaction checks and feedback to Neuro

Status: to verify
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

## Result (2026-10-24)
Built and committed with todo 25. Host tests pass; the DLL builds; nothing was run in game. Design note:
`docs/design/walker-recovery-and-feedback.md`.

Built:
- `RayBlocked`/`HasLineOfSight` moved from `CombatHandler` to `WorldUtils` (`LineOfSightTrim`, default = combat's old
  35/60). Both use the stance-aware eye (todo 27). Walker passes 15/25 because its rays are only about a metre long; the
  old trims made a 100-unit ray count as "within reach, clear" and would have checked nothing.
- `TryComplete`: Interact needs the view on the target and a clear ray; after `kAlignmentGiveUpDelay` (5 s) it activates
  anyway and logs which of the two was missing. Move counts as reached with a clear ray or a height difference up to
  60 units; otherwise it fails with `WrongFloor` after 5 s.
- `Walker::Event` carries `reason` (`FailureReason`), `description` and `detail`; `GameplayHandler::ProcessWalkerEvents`
  builds the text with `WalkerLogic::DescribeFailure`: stuck, door locked (naming the door), no progress, target gone,
  not reachable from here, activation rejected, not interactable. The quest door-step wording for Started/Completed is
  unchanged and now applies only to those two events.
- Progress reminder: non-silent, every 40 s (20 s while `Owner::Exploration`), "still walking to X. Distance walked,
  walk time, distance to target"; never for `Owner::Combat`. Engine-free `ProgressReminder`, host-tested.
- No `Started` event is produced for `Owner::Combat` any more.
- Distances in these messages go through `Units::FormatDistance`, which still prints game units. **Todo 39 changes that
  one function** once the factor is sourced; nothing else needs to move.

Not done / for later: the poke system (todo 24) should treat a recent reminder as activity; no fast-travel hint beyond
Skyrim's 400 m (not asked for).

### Manual in-game checklist
1. Walk to an object on the far side of a railing or on another floor: it does not report reached/interact until a clear
   ray exists; after 5 s a plain move fails with the "not reachable from here" message, an interact activates anyway
   (log: `Walker could not line up ... line of sight blocked`).
2. A normal interaction (container, door, NPC) still completes within a couple of seconds; the log shows no repeated
   `line of sight ... changed` flapping at close range.
3. A walk of over 2 minutes (explore): a reminder arrives about every 20 s (40 s for move/quest) with plausible numbers
   and is not silent.
4. Force a stuck failure (todo 25 checklist): the message names the cause.
5. Melee `attack`: no "Closing in on ..." context messages appear any more.

## Follow-up after the first play-test (2026-10-25)
Designer's report: following a moving quest NPC, she kept walking behind them even about a metre away and never
interacted. Cause: interaction required distance under 100 units for a full second with the view settled on the target;
a walking actor leaves that range before the second is up, and the timer reset every time.

Change: for `Intent::Interact` on an **actor** only, she engages at 150 units (`kActorInteractionDistance`), stops
walking, waits only for a clear ray (no view alignment) and interacts after 0.3 s. Once engaged the range grows to 250
(`kActorInteractionRelease`) so a step back by the NPC does not cancel it. Everything else (doors, containers, plain
moves, combat approaches) keeps the 100-unit rule. The stuck/progress checks treat the engaged state as "arrived".

Unverified: that the game accepts an activation (dialogue) from 150 units, and whether 150 feels natural on stream. If
the activation is rejected from that far the failure will read "The game rejected interaction"; lower the constant.
Check: follow a walking quest NPC; she should stop and talk within a second or two of getting close.
