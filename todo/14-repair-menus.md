# 14 - Repair menus (item repair and repair services)

Status: open

Menu ids 1035 (`PipboyRepair`, `repair_menu.xml`, `RM_CancelButton`) and 1058 (`VendorRepair`, `repair_services_menu.xml`, `RSM_RepairAllButton`/`RSM_DoneButton`). Currently stubbed with `close_repair` and `close_repair_services`.

## Goal
Let Neuro repair gear with duplicates/repair kits (item list, condition, result) and pay an NPC to repair (cost, repair all).

## Notes
- The Pip-Boy item repair flow starts from `InventoryHandler` territory (it excludes repair popups today); read
  `.claude/rules/pipboy-inventory.md` first.
- Verify the id-to-menu mapping (1035 vs 1058) in game.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- The menu currently has a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented"
  message) unless stated otherwise. Replace it: give the menu its own handler under `src/Menus/` and **delete its table row**
  in `StubMenuHandler.cpp`, then register the handler in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
