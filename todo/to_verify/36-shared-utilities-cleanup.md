# 36 - Cross-module duplication and hygiene (mechanical)

Status: to verify
Area: whole `src/`
Source: `docs/code-review.md` C1, C4, C9-C16, D

Low risk, one commit per bullet. Do after the bug and feature todos (24-35); nothing here changes behaviour.

## Shared helpers
- **FrameTime**: five copies clamping `secondsPassed` to 0..0.1: `WalkerHandler.cpp:140`, `CombatHandler.cpp:659`
  (exported and used by `VatsHandler.cpp:449`, so a menu depends on combat for a clock), `StanceHandler.cpp:34`,
  `GameplayHandler.cpp:516-518`, `DelayedGuard.cpp:19` (unclamped and unchecked). One `TimeUtils::FrameSeconds()` with a
  named `kMaxFrameSeconds`.
- **`IsTopMenu`** copied in Dialog, Traits, CharGen, Special: use `UIUtils::IsTopMenu`.
- **"Valid ... ids are" builders** (`GameplayHandler.cpp:380-406`, `CombatHandler.cpp:390-402`): one engine-free
  `FormatValidIds`, host-tested.
- **Revalidation lambda** (10 copies in Gameplay and Combat actions): helper in `Action.hpp`.
- **Game-ready probe**: `StanceHandler.cpp:56`, `CombatHandler.cpp:667,863` call `ValidateQuestAction(ignored)`;
  expose `GameplayHandler::IsGameReady()`.
- **Single-id payload parsing**: `SelectQuestAction`/`TargetObjectAction` repeat parse + `IsObject` + `Size != 1` +
  `GetInteger` and neither enforces `id >= 1` although the schema says minimum 1; `CombatLogic.cpp:370-415` has two more
  variants. Add `ActionData::ParsePositiveId`.
- **Definition storage**: some actions use a file-scope `const Definition`, others a `m_definition` member. Pick one.
- **Geometry**: `CombatHandler.cpp:239` inline 3D length, `WalkerHandler.cpp:248` inline 2D length next to
  `Math::GetDistance2D` (pointer args, silent 0 for null) vs `WorldUtils::Distance3D` (refs). Unify signatures.
- **Constants**: `kStepDelay` 350 ms in four files (Inventory correctly reuses PipBoy's), `kRegisterRetryDelay` 2 s in six,
  `kCloseTimeout` 5 s in two, selection delays in Message/Dialog/Traits/CharGen, `using Clock = steady_clock` in eight
  files (`ScopedState::Clock` exists). A `Menus/Pacing.hpp`. `SKILL.md` says to share constants.
- **`PersistentActionSet` boilerplate** in six handlers (built flag, `BuildActions`, register/unregister tail):
  `PersistentActionSet::Sync(bool wanted)` plus lazy build.

## Hygiene
- `plugin.cpp:35-104`: about 25 empty `case` branches; keep the ones that act.
- `math.h:6` `const double PI = std::acos(-1.0)` (dynamic init per TU, no `<cmath>` include) -> `std::numbers::pi_v<float>`.
- `Hooks_DirectInput8Create.cpp:348` bare `0x00FDF02C` thunk address: name it and cite 1.4.0.525; `<deque>` included twice;
  `FramerateTracker::Update` runs on every mouse poll with nobody reading it; `IsKeyPressed`'s unused parameter.
- Raw addresses in handlers: `DialogHandler.cpp:57` (`0x11D9510`), `TraitsHandler.cpp:64` (`0x11DAF74`): move to
  `src/defs/` with provenance like CharGen and Special.
- `UIUtils.cpp:132-133`: `strcpy_s` into a 256-byte buffer hits the CRT invalid-parameter handler on a long name; bounded
  copy. `TextEditHandler.cpp:72` and `CharacterEditorHandler.cpp:24` copy to a `char path[]` although `GetComponent*`
  take `const char *`.
- Plain-text context messages where `SKILL.md` asks for `##` Markdown: Message `:119,239`, Dialog `:144,293`, Traits
  `:188,381`, CharGen `:190,389`, Special `:102,312`.
- `ActionData` walks the element list three times with the same key match: extract `FindElement`; `GetInteger` cannot tell
  wrong type from out of range. `JsonSchema::AppendNumber` can print `nan`/`inf`.
- Small: `ItemQuery.cpp:150` unreachable `page < 0` (repeated at `MenuItems.cpp:79`); `ItemListing.hpp:44-57` `Find`/`Consume`
  duplicate the offset arithmetic; `MessagePopupHandler.cpp:243-246` / `DialogHandler.cpp:297-300` overwritten
  `clickAt`; `unique_ptr<ExecutionState>` where Container/Stub/PipBoy already use `std::optional`;
  `ContainerHandler.cpp:267` and `InventoryHandler.cpp:86-116,174-185` bare category/key numbers (name them);
  `SpecialAllocationHandler.cpp:85-87` literals and duplicate `7`/`1..10` limits in `SetSpecialAction.cpp:192,222`;
  `PopupPolicy.cpp:44-46` lowercases the table on every call; `TraitsHandler`/`CharGenHandler` compute `canSelect`/
  `canFinish` repeatedly; `PipBoyHandler` calls `CurrentTab()` several times per frame; `VatsHandler.cpp:230` unused
  `menu` parameter, `:394` allocates in `ReadListing()` every waiting frame, `:211` `Notify` wrapper used once.
- `Walker::UpdateMovement` releases and re-holds the same keys every frame (`WalkerHandler.cpp:426-448`); only change
  keys on transitions, if the hook is verified to tolerate it.

## Result (2026-10-24)
Done (commits "refactor: one frame clock...", "chore: trim plugin.cpp...", "chore(menus): name raw numbers..."):
- FrameTime: `TimeUtils::FrameSeconds()` (`kMaxFrameSeconds`) replaces the five copies; `DelayedGuard` now clamps too.
- `IsTopMenu`: Dialog/Traits/CharGen/Special call `UIUtils::IsTopMenu` (Special's version no longer answers true for a null
  menu pointer, which no caller relied on).
- Valid-ids builders: `Actions::FormatValidIds` (host-tested). The empty-list wording of the three messages is now uniform.
- Revalidation lambda: `Actions::Revalidator` replaced 21 copies (13 in Gameplay/Combat and 8 in Menu actions).
- Game-ready probe: `GameplayHandler::IsReady()` in Combat and Stance.
- Single-id parsing: `ActionData::ParsePositiveId`/`GetPositiveInteger` (select_quest, the object actions, attack,
  open_vats); `ActionData` extracted `FindElement` and `ReadInteger` tells wrong type from out of range.
- Definition storage: the five parameterless actions that held a member now use a file-scope const; the ones that depend on
  constructor arguments keep a member (rule in the skill).
- Geometry: `Math::GetDistance2D` is one header template used by Walker for all three inline 2D lengths (CombatHandler no
  longer has an inline 3D length); `Math::PI` is `constexpr`, still a double.
- Constants: `Menus/Pacing.hpp` (step, selection, register-retry, close timeout, one `Clock`).
- `PersistentActionSet::Sync`/`Empty` replace the built flag and the register/unregister tail in six handlers.
- Hygiene: `plugin.cpp` switch trimmed; the DirectInput hook names its thunk address (cites 1.4.0.525), lost the unread
  `FramerateTracker` and an unused local (the duplicate `<deque>` include was in `InputCore.hpp` and the hook, now only
  there); `MenuSingletons.hpp` holds the Dialog/Traits singleton addresses with provenance; `UIUtils::GetControl` uses a
  `std::string` copy (no more `strcpy_s` into 256 bytes); the five context messages that were plain text are Markdown;
  `JsonSchema` drops non-finite bounds and prints `null` for a non-finite number (tests first); the small items
  (`MenuItems` paging moved to `ItemQuery` with tests, `ItemListing` slot rule, overwritten `clickAt`, `std::optional`
  execution state, named inventory categories and the container filter, SPECIAL limits in `SpecialLimits.hpp`, popup table
  stored lowercase, once-per-frame `CurrentTab`, once-computed `canSelect`/`canFinish`).

Not done, and why:
- `char componentPath[]` in `TextEditHandler`/`CharacterEditorHandler`: the todo's premise is wrong. `Tile::GetComponent`
  writes NULs into the path while walking it (JG `GameTiles.cpp`), so a writable copy is required; both sites now say so.
- `ItemQuery.cpp:150` `page < 0`: no longer exists there; the check in `MenuItems.cpp` went away with the move (a negative
  page wraps and is still refused).
- `VatsHandler`: `Notify` is used four times (not once) and `ReadListing()` allocates nothing while the list is empty,
  so neither changed.
- `Walker::UpdateMovement` keeping keys held across frames: only "if the hook is verified to tolerate it"; it is not.
- `ShouldIgnoreKey`'s unused parameter (the call passes a key index, the function ignores every key by design).

Manual in-game checks (regressions, nothing should look different): `query_quests` / `query_nearby` / `query_enemies`
with a stale id (the correction text lists valid ids); every menu handler still opens, selects and finishes (SPECIAL,
traits, tag skills, a dialogue line, a popup, container loot, Pip-Boy inventory, close_container); mouse and keyboard still
work with the DirectInput hook (no stuck keys).

## Done when
`just debug` and `bash tools/test.sh` still pass, no behaviour changed, and the duplicated names above exist once.
