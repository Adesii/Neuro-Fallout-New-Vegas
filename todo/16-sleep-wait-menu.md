# 16 - Sleep / wait menu

Status: open

Menu id 1012 (`Interface::SleepWait`), XML `sleep_wait_menu.xml` (`SWM_WaitButton`, `SWM_CancelButton`). Currently stubbed with `close_sleep_wait`.

## Goal
Let Neuro rest or wait a chosen number of hours (and see the result: health, time of day), and cancel.

## Notes
- Needs an hours parameter (validate range locally); sleeping also matters for owned beds and radiation/healing context.
- Check how the hour selection is done in the XML before choosing the control flow.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- The menu currently has a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented"
  message) unless stated otherwise. Replace it: give the menu its own handler under `src/Menus/` and **delete its table row**
  in `StubMenuHandler.cpp`, then register the handler in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
