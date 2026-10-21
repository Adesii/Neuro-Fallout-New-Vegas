# 27 - Aim: one target point, stance-aware eye height, safe angle maths

Status: open
Area: Aim / Walker / Combat (`src/utils/AimUtils.*`, `WalkerHandler.cpp`, `CombatHandler.cpp`)
Source: `docs/code-review.md` A6, B5, C13, C14. Extends `todo/03-attack-object-aim.md`.

## Problem
- Combat aims at feet + 70 (character) / 35 (creature) / 20 (object), scaled (`CombatHandler.cpp:48-50,161-169`).
  Walker aims at feet + 100 for any actor, 50 for activators, 20 for flora (`WalkerHandler.cpp:385-403`).
- `AimUtils::kEyeHeight = 100` is constant: wrong when crouched and for scaled actors; pitch uses `player->pos`.
- Candidates for the bottle misses in todo 03: a 20-unit object height, the fixed eye height, and
  `kRangedPitchTolerance = 0.07 rad` (~4 degrees, roughly 35 units at 500 units).
- Walker's `UpdatePitch` re-derives `AimUtils::DesiredPitch`; there are three turn controllers (Walker velocity
  smoother + look-target filter, `AimUtils::AimAt`) with a `kMaxTurnRate` of 3 and 7 under the same name.
- `WrapAngle` (`AimUtils.cpp:25-32`, `math.cpp:406-409`) loops forever on +/-inf and passes NaN through. Two copies.
- Skyrim: bounding-box height, head/torso nodes, projectile lead and bow arc (`lock_camera_onto_target`,
  `WalkerProcessor.cpp:5447-5760`).

## Decisions (designer, 2026-10-21)
- Unify the heights into **one target-point function** used by Walker and Combat, and make the eye height
  **stance-aware**.
- **No projectile lead or arc** for now; reconsider only if grenades/plasma miss after the unification.

## Work
1. **Measure first** (todo 03): log eye position, target position/bounds and resulting yaw/pitch for the Goodsprings
   fence bottles, close and far, standing and crouched. Do not change constants before that data exists.
2. `AimUtils::TargetPoint(TESObjectREFR *, Purpose)` with `Purpose = Look | Shoot`; the height tables live in one
   place with provenance comments. Prefer a bound/skeleton source only if verified in `docs/reference-research.md`
   sources; never invent offsets.
3. `AimUtils::EyeHeight(player)`: derive from sneaking state (`Stance::IsSneakingFlags`, `StanceLogic`) and scale; find a
   verified engine value first (JIP-LN-NVSE / FalloutNVAccess) and record the source.
4. Replace `WalkerHandler.cpp::UpdatePitch`'s own formula with `AimUtils::DesiredPitch`.
5. Name the turn constants by purpose (`kWalkTurnRate`, `kCombatTurnRate`) or share one smoother.
6. Angle wrapping: one implementation with `std::remainder` (finite guard); engine-free + host test (inf, NaN, large
   multiples). This also covers `math.cpp`'s `GetHeadingBetweenPoints` wrapping.
7. Reconsider the pitch tolerance for small free-aim targets after step 1 data.

## Existing precedent
- `AimUtils` already says the aim code is proven by `FalloutNVAccess/src/AimUtil.cpp` (MIT); keep that notice.
- `.claude/rules/combat.md`: Combat owns aim while engaged, Walker must not write yaw/pitch at the same time
  (`SetLookSuppressed`).

## Done when
Walker and Combat agree on where a target is, crouching shoots at the same point, the angle wrap cannot hang, the maths
has host tests, and the bottles in Goodsprings are hit reliably (close + medium) with the numbers recorded in the todo.
