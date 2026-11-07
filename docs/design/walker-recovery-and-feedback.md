# Walker recovery and progress feedback (todos 25 and 26)

Written with the `skyrim-feature-port` skill. The designer's decisions were already recorded in the two todo files
(2026-10-21), so this note documents what was built from them instead of waiting for a second approval.

## Goal
Walker survives rubble, doors and cell changes without help, tells Neuro why it gave up, reports a long walk while it
is happening, and stops activating things through geometry.

## Skyrim behaviour (and why; reasons marked inferred are guesses)
`WalkerProcessor.cpp`, read only, nothing copied:
- Stuck = under 50 units in 1.5 s, ignored 5 s after a load or teleport. A window instead of a per-frame test so it does
  not depend on frame rate (inferred).
- Escalation: forward step, look wiggle across four directions, re-path, then activate a door within 200 units after
  repeated failures. Budget 4 retries, 10 for quest walks (long scripted routes are worth more patience, inferred).
  Then a failure with advice.
- Reminder every 40 s (20 s while exploring): distance walked, walk time, distance to target. Non-silent: a long walk
  otherwise leaves the model with nothing (inferred).
- Interaction requires the crosshair on the target.

## What transfers / what does not
Numbers and the shape of the ladder transfer. The script-layer retry plumbing, the Skyrim fast-travel hint and the
crosshair-only interaction rule do not (the designer chose "prefer line of sight, still give up after 5 s").

## Design as built
Engine-free, host-tested (`src/Movement/WalkerLogic.*`, `tests/WalkerLogicTests.cpp`):
- `StuckDetector`: 50 units / 1.5 s window over real time, grace after a load-door cell change, teleport-jump restarts it.
- `SlideDetector` (2026-11-07): over 2 s, the distance made along the steering direction must be at least half the
  distance covered; less means the player is sliding along something it steers into. Under 50 units covered it leaves
  the verdict to `StuckDetector`.
- `OscillationDetector`: the old distance-flip detector, unchanged numbers.
- `ProgressTracker`: remaining distance must improve 70 units (about 1 m) per 30 s or the walk fails (`NoProgress`).
  It keeps counting during recovery steps. (Was 25 units per 45 s, paused during recovery.)
- `RecoveryPlanner`: door in front (once) -> Jump -> Sidestep -> Sidestep (other side) -> BackOff, repeating, the last
  two attempts Wander. Budget 6 (8 for `Owner::Quest`). The budget comes back only when the player is 10 m closer to the
  target than at its closest stuck point; time and distance walked never refill it.
- Crossing into the next exterior cell on the same walk (same target, no teleport) keeps the planner and the progress
  tracker; only a load door or a new target starts them over.
- History: first budget 4/10 refilled by 200 units travelled, then (2026-10-25) 6/12 refilled after 10 s clean and
  200 units from every stuck spot. Both let a corner loop forever; the designer prefers failing to looping (2026-11-07).
- `ProgressReminder` (40 s / 20 s exploring), `PauseNotice` (one silent note after 30 s on hold), `FailureReason` and the
  text builders `DescribeFailure/Progress/Pause`.
- Distances go through `Units::FormatDistance` (`src/Context/Distance.*`), which prints meters (todo 39).

Engine side (`src/WalkerHandler.cpp`):
- Recovery steps: Jump pulses the bound Jump control (own `InputCore::Owner::WalkerPulse`, skipped when unbound);
  Sidestep/BackOff steer the movement keys a quarter/half turn off the path (works while combat owns the view); every
  step ends by dropping the cached route, so the next search starts where the step left the player (there is no
  separate re-path rung: walking already re-paths every 0.25 s, so it would have changed nothing); Door turns to a closed, unlocked, non-load door within 200 units and about 57 degrees of the
  facing, activates it, waits 0.8 s.
- A locked closed door nearly dead ahead within 130 units fails the walk at once (`DoorLocked`, naming the door); a
  locked one seen further off is named only if the budget runs out.
- Repath uses a 0.1 s floor and skips nodes already within 60 units of the player.
- Interaction and plain moves use the shared `WorldUtils::HasLineOfSight` with shorter trims (15/25 units). Interact
  (`WalkerLogic::InteractionGate`) waits for the aim to be done (view on the target, or the view stopped moving for
  0.25 s) and a clear ray, then activates after 1 s; whatever holds it up, it activates after 5 s within reach. The
  crosshair never has to be on the target: Walker calls `Activate` directly. Move counts as reached with a
  clear ray or a height difference up to 60 units; otherwise `WrongFloor` after 5 s.
- Events carry a reason/description/detail; `GameplayHandler::ProcessWalkerEvents` builds the text. Started events are
  no longer produced for `Owner::Combat`. Progress is non-silent, Paused silent.

## Evidence
- Open state: `GetOpenState(ref)` FalloutNV.exe 1.4.0.525 `0x47B250`, `__cdecl`, results None 0 / Open 1 / Opening 2 /
  Closed 3 / Closing 4: itr-nvse `features/DoorPinchFix.cpp` and JohnnyGuitarNVSE `BGSOpenCloseForm` agree.
- Lock: `ExtraLock` (extra type 42) has `data` at +0xC (BSExtraData is 0xC) and a data block with the key at +4 and
  `flags` at +8, bit 1 = locked: JIP-LN-NVSE `GameExtraData.h` and NVSE `GameExtraData.h` agree on offsets, only JIP
  documents the bit. The lock level width at +0 differs between them and is not read.
- Load doors carry `ExtraTeleport` (type 43) and are never activated during recovery.

## Unverified assumptions
1. The locked bit really is bit 1 of `flags` in the 1.4.0.525 layout.
2. `GetOpenState` returns Closed (3) for a closed ordinary door and something else for sliding/odd doors.
3. The 15/25-unit ray trims give a correct answer at about one metre: a false "blocked" (possibly the target's own
   collision, e.g. a mailbox seen from the side) only costs 5 s before the activation happens anyway (logged as `Walker line of sight to ... changed`).
4. A jump pulse of 0.12 s actually jumps with the player's binding; stock FNV has no sprint or sneak-jump conflict.
5. Whether `Activate` on a door at up to 200 units behaves like the player's own activation.
6. `PlayerCharacter::reticleActor` is only trusted for actors; for doors and containers the ray decides.
7. The pause notice assumes `Walker::Pause` runs every frame while a menu is open (it does, from `NeuroSDK::MainLoop`).

## Manual in-game checklist
See the Result sections of `todo/to_verify/25-...` and `26-...`.

## Open questions
- Todo 24 (done): a non-silent progress reminder counts as activity for the idle poker.
