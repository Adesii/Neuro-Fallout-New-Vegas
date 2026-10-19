# 05 - close_container action

Status: open

## Problem
When a container is open Neuro can only loot or stow; she cannot just close it. She is forced to take something to
get out.

## Goal
A `close_container` action in the container window that leaves the menu without transferring anything, and sends
context that it was closed.

## Notes
- Read `.claude/rules/container-inventory.md` first. Code: `src/Menus/ContainerHandler.*`, `src/Actions/Container/`.
- Follow the required action order (validate, revalidate, close window, send result, then execute) and drive the real
  close/exit control visibly, then confirm the menu left the stack (not the singleton pointer).
- Check the equivalent for other menus Neuro can get stuck in (Pip-Boy has `close_pip_boy`; check dialogue/barter
  /lockpick-type menus for the same gap, and list findings without fixing them here).
- Decide force behaviour: does an existing force on the container window need `close_container` included?

## Done when
Opening a container then `close_container` leaves the menu and resumes Walker; window not regenerated mid-close.
