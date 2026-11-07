# 03 - Attacking objects misses (bottles on the fence)

Status: in progress (first log analysed 2026-11-07; aim now from the engine camera at the bound centre, needs a retest)

## Problem
In the quest "Back in the Saddle" (shooting the bottles on the fence) the automatic `attack` with `object_id` misses:
the aim lands above the bottles. Attacking them directly (manually) works.

## Related
Aim heights, stance-aware eye height and the angle maths are unified in `27-aim-unification.md`; do the
measurements here first and feed the numbers into that todo. Candidate causes found in the code review: object aim
height of 20 units, a fixed eye height of 100 (wrong when crouched), and a ~4 degree pitch tolerance
(`docs/code-review.md` A6).

## Where to look
- `src/utils/AimUtils.*` (aim point computation) and `src/CombatHandler.cpp` free-aim path for non-actor targets.
- Aim is probably computed at the reference origin or bounding-box centre; for small props the origin is at the base
  and some models have an offset origin. Another candidate: pitch is computed from the player's feet/eye height or
  ignores weapon/camera offset or projectile drop and gravity.
- Compare with how Walker/aim looks at actors (`GetCenter`/head position, if any) and reuse what works.
- Check the free-aim burst (5 shots) is not ending before the aim converges, and that the aim is re-evaluated each
  frame rather than once.

## Needs in-game data
Log the player eye position, the target position/bounds, and the resulting yaw/pitch for a bottle on the fence, and
for a distant one. Report the measured error (pitch degrees or units) before choosing a fix.

## Done when
Shooting the Goodsprings fence bottles with `attack` hits reliably at close and medium range; unit test for any new
engine-free aim maths.

## Progress (2026-10-24)
No aim constant was changed. Combat now writes an `Aim <object>:` line to the log every 0.5 s while it free-aims a
non-actor (eye, crouch flag, target origin/scale, aim point offset, distance, yaw/pitch, remaining errors, reticle on
target, plus the crosshair reference). Aim point and eye height come from the unified code described in `27-aim-unification.md`. Needs: the log
lines from shooting the fence bottles (close, far, standing, crouched), then pick the fix from the data.

## First measurement (designer, 2026-11-07)
One `attack` on a Sunset Sarsaparilla Bottle about 10 m away, standing. The shots still missed.
- Modelled eye (-68303.5, 5765.0, 8563.0), so the feet are at z 8463.0. Bottle origin (-68232.9, 5050.4, 8438.1), aim
  point origin + 20 = z 8458.1. Horizontal distance 718.1, drop 104.9, distance 725.7 (matches the log).
- The aim converged on that point within about 1 s (pitch 0.145 rad = atan(104.9 / 718.1), heading and pitch error
  0.0000), and the burst fired before "You finished attacking". So the turn controller and the tolerances are not the
  problem: the view pointed exactly where the model said, and the model is what is wrong.
- `crosshair 00000000` at 10 m: `InterfaceManager::crosshairRef` does not see the bottle at that range, so it cannot
  confirm a hit either way.
- Most likely cause: the eye. Aim was measured from feet + 100, a guess. A real eye higher than that shoots parallel to
  the modelled line and passes over the bottle by the difference, at any distance. That matches the earlier report
  that the shots land above. A second, smaller one: the fixed 20-unit point sits near the top of a bottle that stands
  on its origin.

## Change (2026-11-07)
- Aim pitch is now measured from `PlayerCharacter::cameraPos` (0xDE0). JG and JIP-LN-NVSE define it at the same
  offset, and JIP casts its own crosshair ray from it. `AimLogic::PlausibleCameraOffset` (within 600 units of the
  feet, finite) guards against a stale value, with the old modelled eye as fallback. This covers crouching and third
  person without guessing `kCrouchEyeRatio`.
- Shooting a non-actor object aims at the centre of its mesh world bound (`Get3DSimple()->GetWorldBound()`, JG
  definitions; JohnnyGuitarNVSE reads bounds the same way). `AimLogic::UsableObjectBound` refuses a bound with a radius
  over 300 or a centre further than radius + 30 from the origin, and falls back to origin + 20.
- Unchanged on purpose: actors still use the height table, and `WorldUtils::HasLineOfSight` still starts at the
  modelled eye, so the Walker's interaction checks verified in game do not move.
- The `Aim` log line now also prints the source (`camera` or `model`), the modelled eye z, the raw camera position,
  and the bound (`used` or `none`, centre height above the origin, radius).
- Host tests for the two plausibility checks in `tests/AimLogicTests.cpp`. Builds; not run in game.

Checks:
1. The same bottle, about 10 m, standing: send the new `Aim` line and say where the shots land. It should say
   `from camera` with the camera z above the modelled eye z (that difference was the miss), and `bound used`.
2. Close (about 5 m), far (about 15 m) and crouched: the bottle is hit. Crouched, the camera z should drop.
3. Third person: the bottle is hit, or note how far off it is.
4. If it says `from model` or `bound none`, the offset or the bound is not usable at runtime: send the line, because
   the raw camera values in it show why.
5. Walker regressions (`EyePosition` also feeds the Walker's look pitch): walking and interacting still look natural,
   with no pitch jumps.
