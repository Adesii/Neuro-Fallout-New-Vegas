# 38 - Engine-free extraction and missing host tests

Status: to verify
Area: `tests/`, `src/Movement/`, `src/Inventory/`
Source: `docs/code-review.md` E1-E5

CLAUDE.md: scheduling and policy logic belongs in an engine-free module with host tests. Most of the work lives in the
feature todos; this file tracks what is left over and keeps the tests honest.

## Items
1. Walker stuck/oscillation/recovery maths: done as part of todo 25 (`src/Movement/WalkerLogic.*`).
2. Untested pure logic: `PulseQueue` (todo 32), angle wrap and heading maths (todo 27), the `Valid*Ids` builders
   (todo 36), `RetryPendingResults` re-entrancy (todo 33), `ActionData::GetInteger` edge cases (`-0`, `1.0`, `1e2`,
   leading zeros, duplicate keys), destroying a bound `ActionWindow` (todo 33).
3. `Inventory::FormatItemPage` (`MenuItems.cpp:88-104`) and the paging maths in `ValidateMenuPage` (`:78-86`) are
   engine-free but live in `MenuItems.cpp`, which is not in the test build. Move them into `ItemListing`/`ItemQuery`
   and test them.
4. Fake transport fidelity and signature drift (todo 33 item 8).
5. Tests are listed by hand in `tests/CMakeLists.txt:14-33`; nothing warns about a forgotten new engine-free source.
   Add a small check (CMake glob warning or a script in `tools/test.sh`).
6. `JsonSchema::AppendNumber` nan/inf (a test first, then fix).

## Result (2026-10-24)
1. Walker maths: done with todo 25 (`WalkerLogic`).
2. Untested logic now covered: `PulseQueue` and the angle wrap/height maths (already had tests from 32/27), the id
   builders (`tests/IdListTests.cpp`), `RetryPendingResults` re-entrancy and destroying a bound `ActionWindow` (todo 33's tests),
   `ActionData::GetInteger` edge cases (`-0`, `1.0`, `1e2`, duplicate keys, the int limits; new tests in
   `tests/ActionDataTests.cpp`, which also pin `ReadInteger`'s failure reasons and `ParsePositiveId`). Leading zeros are
   rejected by the JSON parser before `GetInteger` sees them, so there is nothing to test there.
3. `FormatItemPage`, the paging maths and `QueryTypesHelp` moved from `MenuItems.cpp` into `ItemQuery.cpp` and are tested
   (`tests/InventoryTests.cpp`); `ItemListing`'s slot rule is tested too.
4. Fake transport: signatures were already one shared list (`NeuroSDKTransport.inc`) and the preconditions one shared header
   (`TransportRules.hpp`); the fake now also refuses a blank/`[]`/null context message and sends nothing while
   `State().connected` is false, as the real one does (two tests).
5. `tests/CMakeLists.txt` fails configuration for a `tests/*Tests.cpp` that is not listed and warns about an engine-free
   source (`src/Combat`, `src/Context`, `src/Movement`) that is not compiled; `tools/test.sh` prints that warning instead of
   swallowing it. Checked with a throwaway file of each kind.
6. `JsonSchema::AppendNumber` nan/inf: test first, then the fix (todo 36).

Host tests only; nothing here touches the engine.

## Done when
`bash tools/test.sh` covers the listed logic and fails when a precondition of the real transport is violated in the fake.
