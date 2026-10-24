# 27 - Aim: one target point, stance-aware eye height, safe angle maths

Status: in progress (steps 2-6 done; 1, 3 and 7 wait for in-game data)
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

## Result (partly done, 2026-10-24)
Built and host-tested; nothing here has run in the game.
- Step 2: `AimUtils::TargetPoint(ref, Purpose::Look | Shoot)` is now the only place that picks a point on a reference;
  the heights live in `src/Movement/AimLogic.*` (`TargetHeight`, engine-free, tested). **The numbers are unchanged**:
  Look uses Walker's old table (actor 100, activator 50, flora 20), Shoot uses Combat's (70 / 35 / 20). One small
  change: Look heights now scale with the reference like Shoot already did (identical at scale 1).
- Step 3: `AimUtils::EyeHeight(player)` is `AimLogic::EyeHeight(scale, crouching)`. The sneak state comes from
  `Stance::IsSneakingFlags`. **`AimLogic::kCrouchEyeRatio` is 1.0 (no change)**: no verified engine eye height exists
  in this project. JIP's `eyeHeight` (0x0D0) is on a combat-controller struct, and JG's `PlayerCharacter::fEyeHeight`
  has no pinned offset, so neither is read. Set the ratio from the logged numbers (below). Until then crouching
  aims exactly as before.
- Step 4: Walker's `UpdatePitch` uses `AimUtils::DesiredPitch` + `TargetPoint(Look)`; its own formula is gone.
- Step 5: turn constants are named by purpose (`kWalkTurnRate` in `WalkerHandler.cpp`, `kCombatTurnRate` /
  `kCombatTurnResponse` in `AimUtils.cpp`).
- Step 6: one `AimLogic::WrapAngle` / `WrapDegrees` (`std::remainder`, non-finite input returns 0) used by `AimUtils`
  and `math.cpp::GetHeadingBetweenPoints`. Host tests cover inf, NaN and huge multiples.
- Step 1 support: while Combat free-aims a non-actor, `CombatHandler::LogFreeAim` writes an `Aim <name>:` line to the
  log every 0.5 s with the eye position, crouch flag, target origin and scale, the aim point offset, the distance, yaw,
  heading error, pitch, pitch error and whether the reticle is on the target.
- Step 7 not touched: `kRangedPitchTolerance` stays 0.07 rad until the measurements exist.

## Still open
1. **Designer, in game** (also todo 03): shoot the Goodsprings fence bottles with `attack object_id` close (~5 m) and
   far (~15 m), standing and crouched, and send the `Aim` log lines. From them: set `kCrouchEyeRatio`, decide whether
   the 20-unit object height and the 0.07 pitch tolerance need to change, and record the numbers here.
2. Then steps 1 and 7 can be closed and the task moved to `to_verify/`.
