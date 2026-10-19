# 22 - Audit of the remaining menus (Pip-Boy tabs, options, map, credits, loading)

Status: open

Menus not covered by a handler or stub and not listed in tasks 09-21: Pip-Boy Stats and Data tabs (map, radio, notes, quest list), the Pause/options menus (audio, video, gameplay), the world map, Credits, Loading, `TraitSelect`/`Trait` variants and anything else that blocks gameplay (`MenuHandler` falls back to "paused, no actions").

## Goal
List which of these Neuro can realistically meet, and for each decide: handled elsewhere, needs a stub with a close action, or needs
a real handler. Prioritise the Pip-Boy Stats/Data tabs (status, notes, map) and the Pause menu so she can never get stuck.

## Notes
- `PipBoyHandler` is designed for adding tab handlers beside `InventoryHandler` (see `.claude/rules/pipboy-inventory.md`).
- Check `MenuHandler.cpp`'s final fallback; log the top menu id when it triggers to find ones nobody has seen yet.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- The menu currently has a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented"
  message) unless stated otherwise. Replace it: give the menu its own handler under `src/Menus/` and **delete its table row**
  in `StubMenuHandler.cpp`, then register the handler in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
