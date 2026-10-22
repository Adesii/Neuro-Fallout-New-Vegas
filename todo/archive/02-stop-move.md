# 02 - stop_move action

Status: archived (verified in game, 2026-10-22)

## Problem
Once Neuro starts walking (`move_to_object`, explore, etc.) she cannot stop on her own.

## Goal
A persistent `stop_move` action that cancels the current Walker command, releases held movement inputs, and sends
context saying what was cancelled and where she stopped.

## Notes
- Walker lives in `src/WalkerHandler.*`; it has `Owner` values (including `Owner::Combat`). Define what happens when
  the command belongs to combat's melee approach: probably refuse and point at `stop_attack`, or stop both.
- Must always be registered (like `stop_attack`) and succeed as a no-op message when nothing is moving.
- Make sure nothing keeps the movement keys held (see the "release all inputs on every exit path" rule in
  `.claude/rules/combat.md`).
- Interaction with an in-progress activation (door, container): decide whether stopping cancels it.

## Done when
Action available during gameplay, walking stops immediately, no stuck keys, host test if any logic is engine-free.

## Result
- `stop_move` (`src/Actions/Gameplay/StopMoveAction.*`, `GameplayHandler::PrepareStopMove/StopMove`). Registered whenever a
  game is loaded (like `stop_attack`), so it still works while a menu pauses Walker.
- Clears the `do_current_quest` route before `Walker::Stop()`, otherwise the route restarts Walker the next frame, and
  sends a silent "You stopped moving toward X." context. Revalidates owner and target.
- Refuses with a pointer to `stop_attack` when the active Walker command is combat's melee approach (the engagement would
  re-issue it). Added `Walker::GetDescription()`.
- Manual checks: stop during `move_to_object`, during a `do_current_quest` route (must not resume), during `explore`,
  while a menu is open, and during a melee approach (must refuse); no key stays held.
- Verified in game by the designer (2026-10-22): `stop_move` works.
