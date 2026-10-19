# 18 - Caravan card game

Status: open

Menu id 1083 (`Interface::Caravan`), XML `caravan_menu.xml` (includes bet, build deck, game and results sub-menus; all controls handled in code). **Not stubbed**: no exit control; gameplay pauses with no actions.

## Goal
Let Neuro play Caravan: bet, build a deck, play cards and read the board, end or concede.

## Notes
- Large rules surface; start with a safe exit/concede action, then bet and deck building, then card play.
- Probably low priority; used by a few NPCs and quests.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- This menu has **no stub** (see above): the game just pauses with no actions. Add a handler under `src/Menus/`, register it in `src/MenuHandler.cpp`, and make sure a way out (close/exit) exists first.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
