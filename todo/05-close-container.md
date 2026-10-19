# 05 - close_container action

Status: done (code complete, needs in-game verification)

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

## Result
- `close_container` (`src/Actions/Container/CloseContainerAction.*`, `ContainerHandler::PrepareClose`). Clicks the native
  `NOGLOW_BRANCH/CM_ButtonRect/CM_ExitButton` (container_menu.xml) after a 350 ms step, refused while transfers are queued;
  while closing every other container action is refused. Sends "You closed the container." when the menu leaves; 5 s timeout
  reports failure.
- Unverified: that clicking the Exit tile closes the menu (Take All uses the same mechanism).
- Other menus with the same gap: Pip-Boy already has `close_pipboy`. Barter, lockpick, repair, repair services, weapon
  mod, recipe, sleep/wait, slot machine, roulette, companion wheel, tutorial and book menus now have a stub
  (`src/Menus/StubMenuHandler.*`) offering only a `close_*` action. Hacking, terminals, surgery, caravan, blackjack and
  the level-up menu are not stubbed (exit handled in code / state dependent / must not be skipped) and still have no
  actions.
- Manual checks: open a container and call `close_container` straight away; again right after `loot_item` (refused while the
  transfer runs, accepted after); confirm Walker resumes.
