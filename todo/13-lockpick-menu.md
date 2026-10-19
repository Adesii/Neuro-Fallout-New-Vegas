# 13 - Lockpicking menu

Status: open

Menu id 1014 (`Interface::LockPick`), XML `lockpick_menu.xml` (buttons `LPM_ForceLockButton`, `LPM_ExitButton`). Currently stubbed with `close_lockpick`.

## Goal
Let Neuro pick a lock: the sweet-spot minigame is hard through a visual executor, so first decide what she may do: force-lock
(break) if available, auto-pick if it can be done legitimately, or the real minigame.

## Notes
- Report bobby pin count, lock level and skill in context.
- Be careful with the exact controls (mouse rotation vs keys); verify in game. Keep close as the escape hatch.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- The menu currently has a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented"
  message) unless stated otherwise. Replace it: give the menu its own handler under `src/Menus/` and **delete its table row**
  in `StubMenuHandler.cpp`, then register the handler in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
