# 33 - Action registry, scripts and tooling correctness

Status: open
Area: `src/Actions/*`, `src/CachedScripts.hpp`, `tools/`, CMake
Source: `docs/code-review.md` B3, B4, B6, B7, B8, B11, B14, E2

## Problem
1. `~ActionWindow` only clears the force guard (`ActionWindow.cpp:11-14`); `ActionRegistry` keeps raw
   `IAction*`/`ActionWindow*`. A handler that resets a Registered/Forced window without `Release()`/`End()` makes the next
   `Dispatch` call `Validate()` on freed memory. The test covers only the guard.
2. `CachedScripts::Get` inserts a null script before checking `g_script` and never retries after a failed compile; the
   map is keyed by `std::string` so every lookup allocates (`CachedScripts.hpp`). `Call`, `CallNoResult`, `CallC` are
   three near-identical templates.
3. `DeliverOrQueue` ends the window before checking the id (`ActionRegistry.cpp:86-96,118-122`); an empty id closes
   the window without answering or executing.
4. `RetryPendingResults` runs `execute()` after `erase(iter)` while the loop continues (`ActionRegistry.cpp:150-157`):
   a callback that pushes or resets invalidates the iterator.
5. `PersistentActionSet::Register` retries every frame with no backoff and logs a warning each time
   (`PersistentActionSet.cpp:16-55`); callers: Combat, Stance, Gameplay, Container, Inventory, PipBoy, Stub.
6. `PollMessages` destroys only Action messages (see also todo 30).
7. `tools/test.sh:8` sends the build output to `/dev/null`: a compile error exits non-zero with no message.
   `CMakeLists.txt:38-41` has a redundant second glob; no warning flags are set beyond `/permissive`;
   `tools/build.sh` shells out to `python3` only to compare the CMake version.
8. The fake transport (`tests/FakeNeuro.cpp`, `tests/stubs/NeuroSDK.hpp`) is more lenient than the real one (real
   `RegisterActions` rejects an empty list and non-object root schemas, `SendActionResult` refuses empty ids,
   `ForceActions` refuses empty names/query, `UnregisterActions` on an empty list sends nothing) and its signature list
   is hand-copied.

## Work
1. Unbind in `~ActionWindow` for Registered/Forced/Closing windows; guard for static teardown (leaked singleton or an
   "alive" flag). Add the missing test.
2. `CachedScripts`: insert only on success (or keep a failed set with backoff), key by `const char *`/`string_view`,
   collapse the three templates.
3. Registry: reject empty ids before `Validate`; collect finished entries first in `RetryPendingResults`; tests for both.
4. Put `ScopedState::Backoff` inside `PersistentActionSet` (it already exists and is tested).
5. `tools/test.sh`: print the build log on failure; drop the redundant glob; consider warning flags only for new code.
6. Extract the shared transport interface into an engine-free header used by both builds and mirror the real
   preconditions in the fake.

## Existing precedent
- `.claude/skills/neuro-fnv-actions/SKILL.md` (required order validate, revalidate, close/unregister, result, execute);
  `tests/ActionRegistryTests.cpp`, `tests/ActionWindowTests.cpp`.

## Done when
The window-destruction test fails before and passes after, registry edge cases are covered, a broken build in
`tools/test.sh` prints its errors, and persistent sets no longer spam failed registration.
