# 32 - Input injection safety (held keys, pulses, hook)

Status: to verify
Area: `src/utils/InputUtils.*`, `src/hooks/Hooks_DirectInput8Create.*`, `ResetAutomation`
Source: `docs/code-review.md` B1, B2, B13

## Problem
- `PulseQueue::Pulse` on a macro that is already pulsing pushes a second entry; when the first expires
  `SetControlHeld(false)` runs while the second still has time (`InputUtils.cpp:32-46`). The hook's `hold` is a bool, not
  a counter, so Walker, Combat, Stance and VATS can cancel each other's presses.
- Six owners release their own keys (Walker `heldMovementKeys`, combat `g_attackMacro`, pulse queues in Combat,
  Stance, VATS) and `ResetAutomation` (`NeuroSDK.cpp:183-192`) relies on each. `.claude/rules/combat.md` and
  `CombatHandler.cpp:144` already call a stuck mouse button the worst failure.
- Latent: `GetDeviceData` ignores `m_deviceType`; `m_bufferedPresses` is never cleared and unbounded
  (`Hooks_DirectInput8Create.cpp:210-212,514-565`). Nothing calls `BufferedKey*` yet.
- `ReadDeviceState` re-acquires devices the game released on purpose (`:127-136`); unverified in game.

## Work
1. Hook: `DIHookControl::ClearAllScripted()` (held states, taps, buffered queue); call it at the end of
   `ResetAutomation`, on `PreLoadGame`, and on disconnect.
2. Key ownership: make `hold` a counter (`AddHold`/`RemoveHold`) or give every owner its own token so releasing never
   cancels another owner. `PulseQueue` refreshes an existing entry instead of duplicating it.
3. Extract the input-state core without dinput types so it can be host-tested (`KeyInfo::Process`, peek, overflow,
   unavailable device). Test `PulseQueue` (overlap, ReleaseAll) host-side.
4. Before any caller uses `BufferedKey*`: one queue per device type, bounded length, cleared by the central release.
5. Add alt-tab, console and minimise to the manual checklist for item 4 of the problem list (re-acquire behaviour).

## Existing precedent
- `.claude/rules/combat.md`: every exit path must release held inputs (pause, reset, disconnect, death, load,
  retarget).

## Done when
Overlapping pulses behave, a forced disconnect/load mid-fight leaves no key down (checked in game), and the input
core has host tests.

## Result (2026-10-22)
Built and committed. Host tests: 123 pass; DLL builds. Nothing was run in game.

- `src/hooks/InputCore.hpp` (engine-free): `KeyState` (the old `KeyInfo`) holds an owner bit mask instead of a bool
  (`Owner`: Walker, CombatAttack, CombatPulse, StancePulse, VatsPulse). A key is held while any owner holds it and each
  owner's hold/release is idempotent, so no owner can cancel another and unmatched releases are harmless. A plain
  counter was rejected because the callers assert holds repeatedly (`SetAttackHeld(true)` each frame) and release
  without having held.
- `DIHookControl::SetKeyHeldState(owner, key, held)`; `ClearAllScripted()` clears holds, taps and every buffered queue
  (the disable flags are kept: nothing sets them). It is the last step of `NeuroSDK::ResetAutomation`, which already runs
  for preload, new game, exit to menu and disconnect.
- `PulseQueue` takes an owner and refreshes an existing entry for the same control instead of pushing a duplicate (a
  shorter second pulse never shortens a running one). It moved to `src/utils/PulseQueue.cpp` so it is host-tested.
- Buffered injection: one bounded queue (64 events) per device type; `GetDeviceData` now uses its device type; taps queue
  press and release together or not at all. `BufferedKey*` now take the device type; they are still unused.
- `tests/InputCoreTests.cpp` (14 tests): key state, owners, merge/peek/overflow/null buffer, injection with no real
  events (the unavailable-device case), pulse overlap and cross-queue ownership.

### Unverified
- The hook and `ProcessBufferedData` run only in the game; the extraction is behaviour-preserving by reading but
  untested there.
- Whether `ReadDeviceState` should re-acquire a device the game released on purpose (item 4 of the problem list) is still
  an open question; the code is unchanged.

### Manual in-game checklist
1. Walk with Neuro (W held by Walker), start a fight (attack button held by combat), then kill the connection with
   the server: after the plugin resets, no movement key or mouse button stays pressed.
2. Load a save mid-fight and mid-walk: same check (the character stands still, no auto-fire).
3. VATS plus crouch plus combat pulses in one fight: crouch toggles once per request, VATS keys still work.
4. Alt-tab out and back while Neuro walks, open the console (`~`) and close it, minimise and restore: the game keeps
   responding to the keyboard and mouse afterwards, and Neuro's input still works.
