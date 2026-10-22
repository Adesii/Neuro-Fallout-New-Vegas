# 32 - Input injection safety (held keys, pulses, hook)

Status: open
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
