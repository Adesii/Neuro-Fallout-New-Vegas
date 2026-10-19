---
paths:
  - "src/Menus/PipBoyHandler.*"
  - "src/Menus/InventoryHandler.*"
  - "src/Actions/Inventory/**"
  - "src/Actions/PipBoy/**"
  - "src/Inventory/**"
  - "docs/inventory-management.md"
---

# Pip-Boy inventory scope

- `src/Menus/PipBoyHandler.*` owns shared Stats/Items/Data identification, native opening/closing, 350 ms step pacing and navigation lockout. Reuse `PrepareOpen(Tab)` and add future tab handlers alongside `InventoryHandler`, rather than adding their operations to the inventory executor. The Pip-Boy can report `Interface::MainFour`; use active-menu and visibility state to resolve the main tab and exclude repair/mod popups.
- `open_inventory` is a loaded-gameplay action. `src/Menus/InventoryHandler.*` owns Items-only `query_own_items`, `equip_item`, `unequip_item`, `use_item` and `drop_item`. Shared `close_pipboy` is available on ready main tabs after accepted inventory work finishes. No inventory force is issued.
- Pip-Boy queries reuse the container query schema, filters and 15-stack pages. Query indexes are zero-based pages; mutation indexes are one-based bindings to the last successfully published page. `src/Inventory/MenuItems.*` shares native stack reading and identity resolution with containers. The two menus keep independent snapshots; accepted item indexes are tombstoned and different indexes execute FIFO. Queries and close wait for queued work.
- Inventory navigation, selection, activation and completion observation are separate frame steps. Equip/unequip are explicit desired states, use consumes one aid item, and drop uses native controls for the whole stack. Never confirm an unrelated quantity prompt or choose a different stack by base form alone. Closing/switching tabs, replacement, disconnect and load invalidate inventory work and bindings. Walker pauses on opening acceptance, before the native animation begins.
- Read `docs/inventory-management.md` for evidence, extension points, native stack split/merge observation and the concrete manual in-game checklist. Compilation cannot verify native menu effects or UI-mod compatibility.
