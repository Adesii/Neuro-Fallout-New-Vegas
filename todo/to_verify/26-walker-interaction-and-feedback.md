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

## Follow-up: stuck in front of off-centre objects (2026-11-07)
Designer's report: `interact_with_object` on a mailbox walked up, aimed, and then stood there for good at some angles,
because the crosshair was not on the hitbox. Wanted: close enough and a clear line of sight, then interact one second
after the aiming is finished, crosshair or not.

Cause found in `TryComplete`: whenever the view was off or the ray blocked, the interaction timer was zeroed every
frame, so after the 5 s give-up it only ever held one frame's time and the "interact anyway" fallback never fired. Any
target the view could not line up with exactly was stuck forever.

Change: the decision is the engine-free `WalkerLogic::InteractionGate` (host-tested in `tests/WalkerLogicTests.cpp`,
including a regression test for the fallback). Within reach:
- The aim is done when the view is within the old tolerance, **or** when it has stopped moving for 0.25 s (the turn
  finished short of the tolerance). Activation never needed the crosshair: Walker calls `Activate` on the reference.
- Aim done and a clear ray: interact after 1 s (actors: ray only, 0.3 s, unchanged).
- Whatever holds it up, 5 s after getting within reach it interacts anyway; flickering readiness no longer postpones
  that. Log: `Walker could not line up with X (line of sight blocked | view still turning); interacting anyway`, or
  `Walker aim settled off X (aim error ... rad); interacting` when the settle rule fired.

Unverified: whether the mailbox case was the view or the ray. The last game log (2026-11-07) had no stuck instance.
If the log shows `line of sight blocked` for an object right in front of the player, the 25-unit end trim of the
interaction ray is probably hitting the object's own collision; that would be the next thing to tune (it still
interacts after 5 s meanwhile).

Check:
1. Mailbox from the side and at awkward angles: she interacts about 1 s after the turn stops (at most 5 s), never stands
   there for good. Note which of the two log lines above appears.
2. A container, a door and a terminal straight on: still about 1 s after arriving, no change.
3. A container behind a railing or thin wall within 100 units: interacts after 5 s, as before (the design kept that).
4. A walking quest NPC: still stops and talks within a second or two.

## Playtest (designer, 2026-11-07)
Passed:
- Interacting with a container, door or NPC completes within a couple of seconds, no `line of sight ... changed`
  flapping (check 2).
- A walk of 2+ minutes: non-silent progress reminder every 20 s when exploring and every 40 s for move or quest walks,
  plausible numbers (check 3).
- Following a walking quest NPC: she stops and talks within 1-2 s of getting close, so the game accepts dialogue from
  150 units (`kActorInteractionDistance` stays).
- "got stuck" failures name the cause (check 4, through todo 25's rubble check).

Not run yet: target behind a railing or on another floor (check 1: "not reachable from here" after 5 s for a move,
interact anyway with `line of sight blocked`), melee `attack` with no "Closing in on ..." messages (check 5), and the
mailbox / off-centre object checks from the 2026-11-07 follow-up.
