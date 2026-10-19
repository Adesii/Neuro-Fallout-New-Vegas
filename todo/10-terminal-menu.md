# 10 - Terminal / computers menu

Status: open

Menu id 1057 (`Interface::Computers`), XML `computers_menu.xml`. **Not stubbed**: no exit control in the XML (handled in code); gameplay pauses with no actions.

## Goal
Let Neuro read and navigate terminals: list the current entries/options, select one, read the text shown, go back and exit.
Also covers the terminal hacking entry point (task 09).

## Notes
- Terminals are important for quests and lore, so this is likely the highest-value menu after dialogue.
- Determine how entries are exposed (tile list vs code-rendered text); verify in game and keep indexes stable per page.
- Long text should be sent as context in readable chunks.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- This menu has **no stub** (see above): the game just pauses with no actions. Add a handler under `src/Menus/`, register it in `src/MenuHandler.cpp`, and make sure a way out (close/exit) exists first.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
