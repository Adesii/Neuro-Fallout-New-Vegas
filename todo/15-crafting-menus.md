# 15 - Weapon mod and crafting recipe menus

Status: open

Menu ids 1061 (`ItemModMenu`, `item_mod_menu.xml`, `IMM_CancelButton`) and 1077 (`Recipe`, `recipe_menu.xml`, `RM_ButtonX` craft / `RM_ButtonB` exit). Currently stubbed with `close_item_mod` and `close_recipe`.

## Goal
Let Neuro attach weapon mods and craft from recipes at workbenches/campfires: list mods or recipes with requirements and what is
missing, pick one, craft or install, and leave.

## Notes
- Reuse `src/Inventory/` item classification (weapon mods are already a category).
- Report skill requirements and missing components in the context.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- The menu currently has a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented"
  message) unless stated otherwise. Replace it: give the menu its own handler under `src/Menus/` and **delete its table row**
  in `StubMenuHandler.cpp`, then register the handler in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
