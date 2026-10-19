# 03 - Attacking objects misses (bottles on the fence)

Status: open

## Problem
In the quest "Back in the Saddle" (shooting the bottles on the fence) the automatic `attack` with `object_id` misses:
the aim lands above the bottles. Attacking them directly (manually) works.

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
