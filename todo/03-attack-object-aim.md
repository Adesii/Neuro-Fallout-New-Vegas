# 03 - Attacking objects misses (bottles on the fence)

Status: in progress (measurement logging added; waiting for in-game numbers)

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
