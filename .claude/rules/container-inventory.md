---
paths:
  - "src/Menus/ContainerHandler.*"
  - "src/Actions/Container/**"
  - "src/Inventory/**"
---

# Container inventory scope

- `src/Menus/ContainerHandler.*` owns the six actions in `src/Actions/Container/`: `loot_item`, `query_items`, `loot_all`, `query_own_items`, `stow_away_item`, and `close_container`. They are registered only while ContainerMenu is the top menu, not in the Pip-Boy or barter menu. No container action force is issued.
- Opening a container publishes at most 15 stacks, or an explicit empty message. Query `index` is a zero-based page; item-transfer `index` is one-based (page 0 lists 1–15, page 1 lists 16–30). Optional `type` defaults locally to `All`; the shared `Inventory::ItemQueryType` values are `All`, `Weapons`, `Equipment`, `Consumerable`, `WeaponMods`, `Keys`, and `Misc`.
- Categories use engine form types, not item names: `Weapons` includes weapons and ammunition; `Equipment` includes armor and clothing; `Consumerable` includes aid items (food, drinks, chems) and ingredients; `WeaponMods` includes weapon attachments; `Keys` includes keys. Books, notes, bottles, junk, miscellaneous crafting components, currency and other remaining inventory forms fall under `Misc`. Native form classification lives in `src/Inventory/ItemClassification.cpp`; shared query parsing/schema construction lives in `src/Inventory/ItemQuery.cpp`.
- Container and own-inventory listings are independent, last-published-page snapshots. Transfers tombstone accepted indexes rather than compacting them; only a new query for that source replaces its bindings. Stowing requires `query_own_items` first. Opening supplies the initial loot bindings; indexes outside that listing require `query_items`.
- Transfers capture source pane, form ID, extra-data identity and stack count, then resolve against live menu rows before visual selection and activation. Ambiguous, removed, or changed stacks fail rather than substituting another item. Accepted different-index transfers run FIFO, using native quantity controls for whole stacks; queries and Take All wait until queued transfers finish.
- Take All uses the native `CM_TakeAllButton`, not direct inventory mutation. Popup handling remains native; the executor only confirms a quantity prompt after its own item activation. Menu closure, container replacement, disconnect and load reset both snapshots and queued work. Engine UI behavior still requires manual in-game verification.
- `close_container` clicks the native `CM_ExitButton` (`NOGLOW_BRANCH/CM_ButtonRect/CM_ExitButton`, container_menu.xml) after a visible step. It is refused while transfers are queued, and while it runs every other container action is refused (`g_close`). Closing sends its own context when the menu leaves; a 5 s timeout reports failure and keeps the container open.
