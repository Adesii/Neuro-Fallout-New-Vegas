# 21 - Tutorial, book and SPECIAL book menus

Status: open

Menu ids 1059 (`Tutorial`, `tutorial_menu.xml`, `TM_next_button`/`TM_prev_button`/`TM_close_button`), 1026 (`BookMenu`, `book_menu.xml`, `book_exit`/`book_take`/`book_prev`/`book_next`) and 1060 (`SPECIALBook`, **not stubbed**). Tutorial and book are stubbed with `close_*`.

## Goal
Let Neuro read what these show: send the tutorial page text and the book/note text as context (paged), allow next/previous and
taking a book from the world, then close. The SPECIAL book (skill book effect) needs its own look.

## Notes
- Books can hold quest information, so reading them is useful, not only closing them.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- Slot machine/roulette (17) and tutorial/book (21) have a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented" message). Replace each with its own handler and **delete its table row**; the others listed above have no stub yet (blackjack, SPECIAL book). Register new handlers in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
