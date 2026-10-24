# 25 - Walker: stuck detection, recovery and timeouts

Status: to verify
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

## Result (2026-10-24)
Built and committed with todo 26 (same files). Host tests pass (`tests/WalkerLogicTests.cpp`); the DLL builds. Nothing
was run in game. Design note: `docs/design/walker-recovery-and-feedback.md` (evidence, assumptions).

Built:
- `src/Movement/WalkerLogic.*` (engine-free): `StuckDetector` (50 units in 1.5 s over real time, grace after a cell
  change, teleport-sized jumps restart the window), `OscillationDetector`, `ProgressTracker` (25 units of improvement
  per 45 s), `RecoveryPlanner`, `PauseNotice`.
- Recovery ladder: closed unlocked door in front (once) -> Jump -> Sidestep -> Sidestep (other side) ->
  BackOff, then repeats. Budget 4, 10 for quest routes; 200 units of real travel hands the budget back. Each step has
  its own duration, and the stuck history restarts after every step.
- Door step: only ordinary doors (no `ExtraTeleport`), within 200 units and ~57 degrees, closed (`GetOpenState`) and not
  locked (`ExtraLock` flag). A locked door dead ahead within 130 units fails at once with "the door X is locked";
  further off it is named only if the budget runs out.
- Jump uses `InputUtils::ResolveControl(Jump)` on its own pulse owner and is skipped when unbound. It is recovery-only,
  as decided. While combat owns the view, no step writes yaw (sidestep and back-off steer the movement keys).
- Repath: every recovery step ends with a fresh route search from where it left the player; there is no separate
  "re-path" rung (walking already re-paths every 0.25 s, so it would have changed nothing). 0.1 s floor, and path nodes already within 60 units of the player are skipped.
- Progress-stall timeout fails with "never got meaningfully closer". A command on hold for 30 s sends one silent
  "your walk is on hold: <menu / Pip-Boy / controls disabled>" note (wall-clock timer, because game time stops in menus).

Differences from the plan: the "look wiggle" is a sidestep of the movement keys instead (no yaw writes, so it also
works under combat's aim ownership). Grace applies after a cell change; a fresh command gets none.

Unverified (needs the game): the lock bit and open-state values (see the design note), the recovery timings, whether a
0.12 s jump pulse jumps, and the 50-unit/1.5 s stuck threshold while sneaking.

### Manual in-game checklist
1. Goodsprings: `move_to_object` to something behind rubble or a low obstacle. Log shows `Walker recovery attempt` lines
   with steps; she gets past or fails with "got stuck", never loops silently.
2. A closed, unlocked, non-load door in the way (a shack door): walking toward something behind it opens the door
   (`Walker opened the door ...` in the log) and continues.
3. A locked door in the way: the walk fails with "the door "X" is locked and in your way".
4. A ledge that needs a hop: she jumps in recovery (check the Jump binding; unbind it and confirm the step is skipped).
5. Walk right after loading a save, and through a load door: no false stuck/recovery in the first seconds.
6. Open a menu for over 30 s during a walk: one silent "on hold" note; closing the menu resumes the walk.
7. Stand on something unreachable (target visible across a gap) and wait: the walk ends with the "never got closer"
   message after about 45 s.
8. A long quest-route walk around a hill or fence (the path bends away from the target): it must not end with "never got
   meaningfully closer". The progress stall measures straight-line distance (25 units per 45 s), and a failure on a
   quest route also clears the route. If it fires wrongly, switch it to `PathResult::distance` (valid when not -1).
9. Walk while heavily encumbered or with crippled legs (slow enough to cover under 50 units in 1.5 s): no false
   recovery; if there is one, the stuck window needs a speed-relative threshold.
10. Combat melee approach through an obstacle still ends (combat's 20 s stall) and nothing writes the view meanwhile.
