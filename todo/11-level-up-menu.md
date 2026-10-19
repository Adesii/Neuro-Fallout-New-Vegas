# 11 - Level-up menu

Status: open

Menu id 1027 (`Interface::LevelUp`), XML `levelup_menu.xml` (buttons: `LUM_ContinueButton`, `LUM_BackButton`, `LUM_ResetButton`). **Deliberately not stubbed**: level-ups must be completed, not skipped, so there is no close action.

## Goal
Let Neuro spend skill points and pick perks (and see her new level) and finish the level-up.

## Notes
- Mirror the character-creation flow (`CharGenHandler`, tag skills via `select_skill`) where sensible.
- Provide queries for skills with current values and available perks with requirements.
- Decide whether to force an action when the menu opens (the game is blocked until she decides).

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- This menu has **no stub** (see above): the game just pauses with no actions. Add a handler under `src/Menus/`, register it in `src/MenuHandler.cpp`, and make sure a way out (close/exit) exists first.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
