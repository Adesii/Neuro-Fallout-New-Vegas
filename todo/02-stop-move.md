# 02 - stop_move action

Status: open

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
