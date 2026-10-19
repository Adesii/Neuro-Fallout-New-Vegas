# 20 - Companion wheel

Status: open

Menu id 1075 (`Interface::CompanionWheel`), XML `companion_wheel_menu.xml` (`CWM_Button_Exit`). Currently stubbed with `close_companion_wheel`.

## Goal
Let Neuro command companions: list the wheel's options (follow, wait, stand close, combat style, inventory, ...) and select one.

## Notes
- Verify the options and their order in game; report the companion name in context.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- The menu currently has a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented"
  message) unless stated otherwise. Replace it: give the menu its own handler under `src/Menus/` and **delete its table row**
  in `StubMenuHandler.cpp`, then register the handler in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
