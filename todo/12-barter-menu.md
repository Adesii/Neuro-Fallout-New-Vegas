# 12 - Barter / trading menu

Status: open

Menu id 1053 (`Interface::Barter`), XML `barter_menu.xml` (buttons `BM_ButtonX` accept, `BM_ButtonB` exit). Currently stubbed with `close_barter`.

## Goal
Buy and sell: list the vendor's stock and her own items with prices and caps, add items to the trade, show the balance, confirm
or cancel.

## Notes
- Reuse `src/Inventory/` for listing and classification (see `.claude/rules/container-inventory.md`).
- Quantity prompts use the quantity menu; keep the same safety rules (never confirm an unrelated prompt).
- Barter skill and caps should appear in the context; vendor caps limit sales.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- The menu currently has a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented"
  message) unless stated otherwise. Replace it: give the menu its own handler under `src/Menus/` and **delete its table row**
  in `StubMenuHandler.cpp`, then register the handler in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
