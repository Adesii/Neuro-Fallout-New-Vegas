# Pip-Boy inventory management

Normal inventory uses native Pip-Boy menus. It does not call actor equip/remove
functions or write inventory or menu-filter fields directly. Item selection,
category changes, activation, quantity prompts, opening and closing are visible,
frame-driven steps.

## Actions and index contract

| Action | Parameters | Availability / behavior |
| --- | --- | --- |
| `open_inventory` | None | Loaded gameplay; animates opening the Items tab. |
| `query_own_items` | `index`, optional `type` | Active Items tab; shows and publishes a page of your inventory. |
| `equip_item` | `index` | Equips one weapon, armor or clothing item. An already equipped item stays equipped. |
| `unequip_item` | `index` | Unequips a weapon, armor or clothing item. An already unequipped item stays unequipped. |
| `use_item` | `index` | Uses one aid item from a stack through native item activation. |
| `drop_item` | `index` | Drops the entire listed stack through native Drop and quantity controls. Quest items and keyring keys are rejected. |
| `close_pipboy` | None | Active Stats, Items or Data tab, after accepted inventory work finishes; animates closing and waits for completion. |

`query_own_items` intentionally uses the same name and schema as the container
feature. Its query `index` is a **zero-based page**, containing at most 15 stacks.
Item-action `index` is **one-based**: page 0 lists 1–15, page 1 lists 16–30.
Opening inventory supplies an initial page 0 with `type: All`. A failed initial
query leaves query and close actions available for recovery.

Filters are shared with container looting: `All`, `Weapons`, `Equipment`,
`Consumerable`, `WeaponMods`, `Keys`, and `Misc`; omitted `type` defaults to `All`.
The existing `Consumerable` spelling is preserved for compatibility. Weapons
includes ammunition; equipment includes armor and clothing; consumables include
food, drinks, chems and ingredients; weapon mods and keys remain separate.
Classification uses native form types. There is no native All tab: the published
query can span categories, while visual navigation selects the first stack on
the requested page in its native category. Keys navigate through the Misc
category's native keyring.

Examples:

```text
open_inventory {}
query_own_items {"index": 0, "type": "Equipment"}
equip_item {"index": 2}
query_own_items {"index": 0, "type": "Equipment"}
unequip_item {"index": 2}
close_pipboy {}
```

Wait for visible execution and its context before the next query or close.
Each listing is a snapshot, replaced only when new listing context is sent
successfully. Accepted item indexes are tombstoned; they are never compacted or
reassigned. Query again to reuse an accepted stack or refresh equipped flags.
Different accepted item indexes execute FIFO. Queries wait for that queue to
finish. No inventory action force is issued.

A successful `action/result` means accepted for visible execution. Validation
and revalidation happen before that result; engine execution starts afterward.
Completion or deferred failure is reported through later context after observing
the native state.

Inventory and container action sets are scoped to their respective menus. A
container's own-items listing never supplies Pip-Boy bindings. Closing inventory,
switching main Pip-Boy tabs, menu replacement, disconnect and load clear the
Pip-Boy snapshot and queue. Higher-priority popups suspend execution while the
inventory remains visible. Incoming malformed, duplicate-field, unavailable and
stale-index requests receive failed action results.

## Build and try locally

Check out the feature branch and initialize the SDK:

```bash
git fetch origin
git switch codex/visual-pipboy-inventory
git submodule update --init --recursive
```

With the supported x86 MSVC-over-Wine toolchain installed, build the DLL:

```bash
bash tools/build.sh Release
```

For toolchain prerequisites and optional setup, see
[cloud-development.md](cloud-development.md#optional-windows-x86-compilation).
The default output is `build/cloud/neuro-fnv.dll`. Install it in the local game's
or mod's `nvse/plugins/` directory, replacing the existing plugin while the game
is closed. Keep the existing xNVSE, Neuro connection and `Neuro_FNV.esm` setup;
this feature changes the DLL and requires no ESM update. Alternatively, set
`FalloutNVNeuroPlugin` to the local mod root before configuring to enable the
existing build-time deployment.

Launch the usual xNVSE game setup and load a save. The action examples above are
**Neuro action requests**, not game-console commands. `open_inventory` becomes
available in gameplay; it opens the Pip-Boy and publishes the initial item list.
Query a filter/page, then use a one-based index from that published list to
equip, unequip, use or drop an item. Wait for completion context, query again to
refresh the listing, and use `close_pipboy` when finished.

The PR contains source changes; no prebuilt DLL was produced in Cloud. Run the
manual checks below before relying on inventory automation in a game session.

## Visual execution and extension

`Menus::PipBoyHandler` owns the main `Stats`, `Items` and `Data` tab identifiers,
native open/close transitions, ready-state detection, a shared 350 ms step delay
and navigation lockout. `PrepareOpen(Tab)` is reusable by future tab actions.
`IsExecuting()` aggregates navigation and tab work for gameplay and closing.
`CurrentTab()` handles both concrete tab IDs and the engine's shared `MainFour`
root using active-menu and visibility evidence. Retained singleton pointers do
not establish that a menu is open. Repairs, mods and other popups are not treated
as active inventory.

`Menus::InventoryHandler` owns inventory observation, its action set, snapshot
and queue. Future Stats or Data handlers belong alongside it; integrate their
processing and execution lockout into the shared Pip-Boy handler, without
putting their operations in the inventory executor. Concrete action modules stay
under `src/Actions/Inventory/` or `src/Actions/PipBoy/`.

The executor switches categories, waits, highlights and scrolls to a live stack,
waits again, activates its native control, then observes completion. Quantity
increments use 80 ms steps. Query publication follows visible navigation and
selection. Walker pauses immediately on opening acceptance, including the delay
before the native animation starts, and remains paused until menu execution and
the actual menu close finish. Inventory actions have a 60-second timeout; shared
navigation has a 15-second timeout. Timeouts clear local work and publish recovery
context. An owned unconfirmed Drop quantity prompt is cancelled on timeout.

Form ID, extra-data identity and stack quantity are revalidated against live
menu rows before selection and activation. Ambiguous stacks are rejected. After
an owned equip click, a plain stack can create one new worn extra-data instance;
completion accepts only an unambiguous split with conserved quantity and other
instance identities. Unequipping can merge that instance back into a plain
stack; completion verifies the corresponding merge. These observations never
substitute a different stack for selection. Use and Drop verify the exact
expected quantity change before reporting completion. Explicit equip/unequip
verbs avoid blind toggling.

The existing message-popup handler retains responsibility for native warning
and confirmation dialogs. The inventory executor confirms a quantity prompt
only after its own Drop activation, and only when the maximum matches the
captured stack count. The native equip/drop buttons are controller-only in the
supplied vanilla XML; item activation uses a visible row, and Drop uses the same
native button handler as the shortcut after verifying native selection.
Closing an observed quantity prompt without the expected whole-stack decrease
stops queued work promptly; it never repeats the Drop activation automatically.

## Evidence and verification boundary

The implementation uses existing vendored declarations and shared code:

- `libs/nvse/JG/nvse/GameUI.h`: `InterfaceManager::OpenPipboy`, `ClosePipboy`,
  `InventoryMenu`, `Menu`, native list selection/scrolling and quantity data.
- `libs/nvse/JG/JG/internal/Game/Bethesda/ItemChange.*`: `GetWorn`, item counts
  and extra-data lists; `TESForm.hpp`: native form types and `GetQuestObject`.
- `src/Inventory/ItemQuery.*`, `ItemClassification.cpp`, `ItemListing.hpp` and
  `MenuItems.*`: shared filters, strict schemas, snapshots and live identities.

Corroborating read-only reference facts at the prepared pinned revisions:

- JohnnyGuitarNVSE `de352b278e2452abcf95cb96c6328983923b09ec`,
  `JG/functions/fn_gameplay.cpp`, `Cmd_TogglePipBoy_Execute`: native callbacks,
  requested menu IDs, mode 0 closed and mode 3 ready. `JG/functions/fn_ui.cpp`
  corroborates `MainFour` plus inventory visibility and native list selection.
- JIP-LN-NVSE `5a30ac4356ea0e93b9ff357b5031b1e420240a4d`, `nvse/GameUI.h`
  (`InventoryMenu::tabScrollPos`), `nvse/GameUI.cpp`
  (`InterfaceManager::GetTopVisibleMenuID`) and `functions_jip/jip_fn_ui.h`
  (`Cmd_GetMenuItemListRefs_Execute`): category order, active-menu precedence and
  retention of filtered inventory rows.
- FalloutNVAccess `d24c62ef30394241d8b4920415acd1367fd0c776`,
  `src/menus/InventoryMenuHandler.cpp`: category order and native item/marker
  observation. This reference targets Vanilla UI Plus; its mod-specific paths
  are not used by this feature.
- yUI-NVSE `074e5e45286e4dfa8813a0fd58a11a78df8f9a67`,
  `CommonLib/Bethesda/Menu.hpp`: mouseover's tile-ID parameter;
  `yUI/SortingIcons/SortingIconsMechanics.cpp`: category buttons' `listindex`
  and null-object grouping behavior. No reference implementation is copied.

Privately supplied `menus/main/inventory_menu.xml` and
`menus/prefabs/tabline{,_template}.xml` corroborate `IM_Tabline`, native
category buttons, `IM_InventoryList`, `_EquippableItem`, `_KeyringOpen`, item
markers and native Equip/Drop/Cancel controls. `menus/quantity_menu.xml`
corroborates quantity controls. Private XML is research evidence, not committed
or runtime-tested code.

Cloud cannot run FNV. Native animation completion, mouseover/item-card updates,
stack splitting/merging, confirmation side effects and active UI-mod behavior
require the checks below. Formatting, source checks and an MSVC build do not
establish those runtime behaviors. UI overhauls that reorder native categories,
introduce several grouping rows or hide items through search may cause an
explicit failure instead of selecting another stack.

Checks performed in the prepared Cloud workspace on 2026-10-07:

- Offline source, SDK and reference readiness checks passed; optional MSVC was absent.
- Changed C/C++ files passed `clang-format` and whitespace checks.
- An isolated portable check of the real `ActionData`, `ItemQuery` and
  `ItemListing` components passed 33 parser cases and snapshot lifecycle checks.
  It used no engine code or mock menus and is not Windows DLL verification.
- `bash tools/build.sh Debug` stopped at the missing `/opt/msvc/bin/x86/cl`
  prerequisite; the Windows plugin was not compiled or linked.
- No in-game checks were run in Cloud.

## Manual in-game checklist

Use FNV 1.4.0.525 with the installed UI and an inventory containing more than
15 stacks, duplicate weapons with different condition/mods, plain stacked
equipment, aid, ammo, mods, keys, miscellaneous and quest items. Observe
`neuro-fnv.log` entries for `Pip-Boy`, `Inventory` and `ActionRegistry`.

| Starting state / action | Expected visible result and failure check |
| --- | --- |
| Loaded gameplay with Walker moving; `open_inventory` | Movement input pauses on acceptance; native opening animation reaches Items; page 0 is visibly selected and published. A navigation timeout or absent actions indicates ready-state/tab detection failure. |
| Items; query page 0, page 1 and every filter | Category selection and scrolling precede context; at most 15 stacks; page 1 starts at item 16. Weapons includes ammo, Equipment armor/clothing, Keys uses the native keyring. Empty filters report no matches; out-of-range pages fail without clicking. |
| Stats, Data, container, barter, repair/mod menu or popup | Inventory mutation/query actions are unavailable. Container keeps its original five actions; `close_pipboy` is available on ready main Pip-Boy tabs. |
| Equipment/weapon listing; equip then query and unequip | Correct stack highlights, pauses, activates once and changes its equipped marker. Already desired states do not toggle. Check both plain stacked equipment and distinct condition/modification instances, including native weapon/slot replacement. |
| Queue actions for different listed indexes | FIFO visible execution; accepted indexes cannot be reused; unaffected indexes retain their bindings. A new query or close during work fails with a wait message. |
| Change/remove a stack after listing; request its old index | Reject changed quantity/identity or ambiguity without activating a neighboring row. Duplicate names must not select a different condition/modded instance. |
| Aid stack; `use_item` | Exactly one item is used; native confirmation/warning dialogs remain visible; completion context follows observed decrement. Non-aid, quest or unusable items fail without reporting completion. |
| Multi-item stack; `drop_item` | Correct stack highlights; native Drop opens its own quantity prompt; amount visibly increases to the full count, then confirms. Entire stack disappears before completion context. One-item drop also works. |
| Quest item/keyring key; drop request | Failed result with a useful reason and no mutation. Ammo and mods also reject equip/unequip. |
| Cancel an owned Drop prompt or open an unrelated quantity popup | Cancellation must not produce success; unrelated prompts are never confirmed by this executor. On refusal/timeout, local work clears and recovery context requests a new query. |
| Higher-priority warning/text/repair popup during execution | No inventory clicks through the popup; work resumes if the same inventory is still open. Native warning handling remains with the existing popup handler. |
| Manually close inventory or change main Pip-Boy tab during work, then reopen | Queue and snapshot clear; old actions/indexes are unavailable; reopening supplies fresh bindings. Retained singleton pointers must not keep Walker locked out. |
| Disconnect/reconnect or load/new game during work | No queued activation from the old session; current action sets restore and inventory is observed afresh. |
| `close_pipboy` after work finishes | Native closing animation completes before Walker resumes. Opening/closing refusal, popup obstruction and timeout must not leave an automation lock after the actual menu is closed. |

Also submit malformed JSON, duplicate/unexpected fields, strings/fractional or
negative indexes, item index 0, unknown type spellings and unlisted indexes.
Expect `success: false`, actionable correction text, and no menu mutation.
