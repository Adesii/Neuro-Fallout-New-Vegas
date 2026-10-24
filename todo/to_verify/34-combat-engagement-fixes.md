# 34 - Combat engagement timing and small fixes

Status: to verify
Area: `src/Combat/CombatLogic.*`, `src/CombatHandler.cpp`
Source: `docs/code-review.md` B10, D (magic numbers)

## Problem
1. First shot delay: `Engagement::Begin` zeroes `m_pulseTimer`, so the first pulse waits the whole "off" time (0.16 s
   ranged, 0.4 s melee, 0.9 s thrown; `CombatLogic.cpp:269-282`).
2. `m_stallTime` counts while combat's own melee approach is walking (`CombatLogic.cpp:249-253`): a long or blocked
   approach ends as `Stalled` after 20 s. Already listed as unverified item 8 in `docs/combat.md`.
3. `ProcessEngagement` calls `ReleaseInputs()` and `Walker::SetLookSuppressed(false)` every frame while nothing is
   engaged (`CombatHandler.cpp:438-442`): `SetControlHeld(kNoControl)` each frame.
4. `OutOfAmmo` needs both `clip == 0` and `reserve == 0`; if the HUD text is absent the reserve is -1 and the fight
   waits for the 20 s stall instead.
5. `ClassifyWeapon` uses raw integers 0, 1, 2, 10, 11, 12, 13 (`CombatLogic.cpp:74-90`); `band >= 4/3/2` couples to the
   threshold table (`:331`).
6. `IsInCombatNow` is a trivial wrapper; `CombatHandler` calls `GameplayHandler::ValidateQuestAction(ignored)` as an
   "is a game loaded" probe (see todo 36).

## Work
1. Start the pulse timer at the off threshold (or begin with the trigger down) so the first shot is immediate.
2. Pause the stall timer while `output.approach` is true and the distance is shrinking; reuse the progress idea from
   todo 25 so the two stall definitions match.
3. Release inputs only on the transition to "not engaged".
4. Reserve unknown (-1): treat a long "reloading" blocker with a still-empty clip as out of ammo, or read the reserve
   from a second source; verify in game.
5. Named `WeaponType` constants mirroring `TESObjectWEAP::eWeaponType` (JG headers have the enum); constants for the
   health-band thresholds.
6. Host tests for 1, 2, 4 in `tests/CombatTests.cpp`.

## Existing precedent
- `.claude/rules/combat.md`: decision logic lives in `CombatLogic`; game reads stay in `CombatHandler`.
- `docs/combat.md` Unverified items 6 and 8.

## Result (2026-10-24)
Built (`src/Combat/CombatLogic.*`, `src/CombatHandler.cpp`, tests in `tests/CombatTests.cpp`):
1. First shot: `m_pulseTimer` starts "ready" and is reset to ready whenever the weapon cannot fire (not only in `Begin`:
   the not-aimed and paused paths zeroed it every frame), so the trigger goes down on the first frame it can. Tests cover a
   fresh engagement, "aimed after 30 frames of lining up" for ranged/thrown/melee, resuming after a menu, and that a pulsed
   weapon still waits its off time between shots.
2. Stall timer: a melee approach that closes in (20 units/s over 1 s windows) no longer accumulates stall time; a blocked
   one still stalls at 20 s. Tests: 100 s of closing in, and a stuck approach.
3. Inputs are released only on the engaged -> not engaged transition (`g_wasEngaged`).
4. Unknown reserve: an empty magazine for 6 s ends as `OutOfAmmo`; refilled magazines and a known reserve never do.
5. `Combat::WeaponType::*` constants (static_asserted against `TESObjectWEAP::EWeaponType`) and `kBand*` constants for the
   health severity words (a static_assert ties the table size to `kBandCritical`).
6. `IsInCombatNow` removed; the game-ready probe is `GameplayHandler::IsReady()` (added with todo 24).

Unverified (all in game): the first-shot behaviour, the approach progress numbers, the 6 s unknown-reserve rule. Manual
checks: items 9-11 of the checklist in `docs/combat.md`.

## Done when
First shot fires immediately on a ready weapon, a long melee approach no longer reports `Stalled`, and the new cases are
covered by host tests. Extend the manual checklist in `docs/combat.md` (first-shot latency, long melee approach).
