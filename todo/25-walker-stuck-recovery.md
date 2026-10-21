# 25 - Walker: stuck detection, recovery and timeouts

Status: open
Area: Walker (`src/WalkerHandler.cpp`)
Source: `docs/code-review.md` A3, A5, D (repath, recovery), E1

## Problem
- "Stuck" is at most 1 unit of 2D movement between consecutive frames, accumulated for 3 s
  (`WalkerHandler.cpp:29-30,321`): frame-rate dependent.
- "Recovery" lasts 1 s and steers at `GetPointNavMesh(cell, waypointMarker->GetPos())` (`:555-560`). The marker holds
  the last path destination, so it is mostly the same route: effectively a pause. Three attempts later (~12 s) it fails
  with a generic message.
- No jump, sidestep, door handling or grace period after a load/cell change.
- The repath fires whenever the player is within 60 units of the cached waypoint (`:243-249`); a close `nodes[1]` can
  repath every frame, and `nodes[1]` is used even when already passed (`:208`).
- No total or progress timeout, and a long Pause() sends nothing.
- Skyrim reference (`WalkerProcessor.cpp`): stuck = under 50 units in 1.5 s (`detect_stuck` :19022-19075), ignored 5 s
  after load/teleport; escalation = forward step (:25229), wiggle across 4 directions (`walk_unstuck` :7584), re-path
  (`walk_again` :7426), activate a nearby door after repeated failures (:25140-25165), retry budget 4 (10 for quest
  walks), then a failure with advice (:26855-26935); jump when the path rises >70 units (`needs_jump` :3298).

## Decisions (designer, 2026-10-21)
- Jump is **stuck-recovery only**, not proactive at ledges (FNV has no sprint in vanilla, so no sprint work).
- Closed **unlocked doors in front of the player are opened** during recovery; **locked doors fail with a message**
  ("the door X is locked") so Neuro decides. Auto-lockpicking waits for the lockpick menu (todo 13).
- Add a **progress-stall timeout** (remaining distance has not improved for a long time) and a **silent notice when a
  command has been paused for >30 s**, naming the reason (menu, controls disabled, ...).

## Work
1. Extract the stuck/oscillation/recovery logic into `src/Movement/WalkerLogic.*` (engine-free, like `StanceLogic`):
   position-over-time window (~50 units / ~1.5 s), grace period after load or cell change, escalation state machine,
   progress-stall tracker. Host tests: window math at different frame rates, escalation order, grace period, stall.
2. Recovery steps: jump (resolve the Jump control through `InputUtils::ResolveControl`; verify it is bound, otherwise
   skip the step), strafe + look wiggle, fresh path, open a closed door that is in front of the player
   (`Walker::CanInteract` / `Activate`, check the lock state through existing helpers or verified fields only).
3. Repath: floor interval (~0.1 s) and skip path nodes already within `kWaypointReachedDistance`.
4. Fix the recovery steering target; a recovery that does not change the route is not a recovery.
5. Pause notice: track paused time per command; one silent `Walker::Event` after 30 s.
6. Do not turn `UpdateMovement`'s per-frame release/re-hold into a transition-only hold unless measured necessary
   (see todo 36 hygiene).

## Existing precedent
- `src/Movement/StanceLogic.*` + `tests/StanceTests.cpp`: pure state machine, host-tested; same pattern.
- `docs/combat.md` unverified item 8 notes combat's stall timer also counts melee approach walking; reuse the same
  progress-stall idea so the two do not disagree.

## Answered (designer, 2026-10-21)
- Door recovery: same as Skyrim for now - a closed door within ~200 units and in front of the player.
- Retry budget: 4 attempts, 10 for quest routes. Adjust after play-testing.

## Done when
Walker survives rubble/doors in Goodsprings and a cave interior without human help, reports a specific reason when it
cannot, host tests cover the logic, and the in-game checklist includes: closed door, jump-needed ledge, locked door,
walk after load (no false stuck).
