# 09 - Hacking menu (terminal lock minigame)

Status: open

Menu id 1055 (`Interface::Hacking`), XML `hacking_menu.xml`. **Not stubbed** because the XML has no exit control (controls are handled in code), so Neuro currently has no actions there and gameplay stays paused.

## Goal
Let Neuro play the word-guessing minigame: list the candidate words and attempts left, pick a word, use dud removal/attempt
reset if the game offers it, and leave the menu.

## Notes
- Find how input reaches the menu (mouse over word tiles vs keys); the XML only shows layout. Check the references
  (FalloutNVAccess may handle hacking) before inventing anything.
- Context should give likeness feedback after each guess and the remaining attempts.
- Needs a way to close/abandon the hack (Escape equivalent) as the first step so Neuro is never stuck.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- This menu has **no stub** (see above): the game just pauses with no actions. Add a handler under `src/Menus/`, register it in `src/MenuHandler.cpp`, and make sure a way out (close/exit) exists first.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
