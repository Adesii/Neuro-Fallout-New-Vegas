# 38 - Engine-free extraction and missing host tests

Status: open
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

## Done when
`bash tools/test.sh` covers the listed logic and fails when a precondition of the real transport is violated in the fake.
