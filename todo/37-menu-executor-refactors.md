# 37 - Menu executor refactors (large merges)

Status: open, low priority, only when touching these menus
Area: `src/Menus/*`, `src/Actions/Menu/*`, `src/Actions/Container|Inventory/*`
Source: `docs/code-review.md` C2, C3, C5, C6, C7, C8

Decision (designer, 2026-10-21): separate low-priority refactors after the bug and feature work. Do not start any of
these unless a task already requires editing the affected menus; the plain dedupes live in todo 36.

## Candidates
1. **Traits vs CharGen** (~400 near-identical lines): `TraitsHandler.cpp:28-476` vs `CharGenHandler.cpp:31-485` (`Option`,
   8-phase `Phase`, `ExecutionState`, `SelectVisibly`, `IsSelected`, `Validate*`, `AdvanceExecution`, `Process`) and the
   identical `SelectTraitAction`/`SelectSkillAction` and near-identical `DoneTraitsMenuAction`/`DoneCharGenMenuAction`.
   Proposal: a tag-selection executor parameterised by list box, selected-state accessor, done control and strings, plus
   one `Actions::Menu::SelectPairAction`.
2. **Dialog vs MessagePopup select flow** (select, wait 1.5 s, verify, click, detect change) and the window-sync block
   repeated in Dialog, Message, Traits, CharGen, Special and VATS ("pending result? Ended -> reset; Closing -> End;
   signature unchanged -> return; End; Backoff; build; Register; on failure Backoff"). Proposal: `Menus::PublishedWindow`
   with `Sync(signature, factory)` on top of `ScopedState::Backoff`, and `VisibleListSelection`.
3. **Close request / native exit button**: `CloseRequest`, `AdvanceClose`, `FailClose` identical in `ContainerHandler`
   (`:46-50,285-326`) and `StubMenuHandler` (`:241-315`). Proposal: shared native-exit helper.
4. **Container vs Inventory skeleton**: pause/timeout block (`ContainerHandler.cpp:160-190` vs `InventoryHandler.cpp:451-480`),
   quantity-prompt result switch (`:197-212` vs `:433-448`), "Query types" help text. Proposal: engine-free step timer
   (also needed by todo 31) and a shared help constant.
5. **Item/Query actions**: Loot/Stow and Equip/Unequip/Use/Drop differ only in name, description and operation; the three
   Query actions only in the handler call. Proposal: parameterised `IndexAction`/`QueryAction`.
6. `UIUtils::ClickAvailable(menu, tile)` replacing the open-coded `IsControlAvailable && ClickTile`
   (Container `:223,321,454,470`, Stub `:309`, VATS `ClickIfAvailable`, Inventory `:188`).

## Rules for doing it
- Preserve the required action order (validate, revalidate, close window, result, execute) and the visible step
  pacing; a refactor must not change a menu's observable behaviour. Verify in game with the existing checklists
  (`docs/inventory-management.md`, container rule).
- Add host tests for any engine-free part (the step timer, paging helpers).

## Done when
Each merge lands in its own commit with the old duplicates deleted, and the in-game checklist for the affected menus
is repeated.
