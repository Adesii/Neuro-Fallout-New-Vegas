# 27 - Aim: one target point, stance-aware eye height, safe angle maths

Status: archived (verified in game, 2026-11-08)
Area: Aim / Walker / Combat (`src/utils/AimUtils.*`, `WalkerHandler.cpp`, `CombatHandler.cpp`)
Source: `docs/code-review.md` A6, B5, C13, C14. Extends `todo/archive/03-attack-object-aim.md`.

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
  heading error, pitch, pitch error, the crosshair reference (`InterfaceManager::crosshairRef`, valid for non-actors
  too) and whether the reticle actor is the target.
- Step 7 not touched: `kRangedPitchTolerance` stays 0.07 rad until the measurements exist.

## Still open
1. **Designer, in game** (also todo 03): shoot the Goodsprings fence bottles with `attack object_id` close (~5 m) and
   far (~15 m), standing and crouched, and send the `Aim` log lines. The pitch error in them is measured against the modelled eye, so also say where the
   shots land (high or low, roughly how far); the `crosshair` field says whether the crosshair itself is on the bottle. From them: set `kCrouchEyeRatio`, decide whether
   the 20-unit object height and the 0.07 pitch tolerance need to change, and record the numbers here.
2. Then steps 1 and 7 can be closed and the task moved to `to_verify/`.

## Follow-up (2026-11-07, todo 03)
The first bottle log showed the aim converging exactly on the modelled point while the shots missed. Aim and line of
sight now start at the engine camera (`PlayerCharacter::cameraPos`, JG and JIP agree on 0xDE0). The designer decided
the guess must not be used anywhere, so step 3's modelled eye (`EyeHeight`, `kCrouchEyeRatio`) is removed with no
fallback. Non-actor shoot points use the mesh bound centre. Details and checks are in todo 03.

## Result of the open steps (2026-11-08)
- Step 1 (measure): done through todo 03. The log showed the modelled eye was the problem.
- Step 3 (stance-aware eye): replaced. There is no modelled eye any more; aim and line of sight start at the rendered
  world camera, which follows crouching by itself. `kCrouchEyeRatio` and `EyeHeight` are gone.
- Step 7 (pitch tolerance): replaced by `Combat::AimGate`. For objects the tolerance is the mesh bound's angular
  radius, and every ranged shot waits for the view to settle. Actors keep 0.05 / 0.07 plus the settle check.
- Line-of-sight rays carry the player's collision group, so crouching no longer blocks them with the player's own
  arms.
- Confirmed in game (designer, 2026-11-08, todo 03): fence bottles hit standing and crouched, the first shot included.
  Not yet confirmed: that the Walker's look pitch and its interaction line of sight feel the same with the camera
  origin, and actor shooting with the settle gate (it will come up in normal play).

## Verified in game (designer, 2026-11-08)
The designer is happy with the aim as it is and asked to archive this. The Walker look pitch with the camera origin and
actor shooting with the settle gate were not checked separately; they come up in normal play.
