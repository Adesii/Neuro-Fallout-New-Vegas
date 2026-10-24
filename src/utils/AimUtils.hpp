#pragma once

#include "Movement/AimLogic.hpp"

class NiPoint3;
class PlayerCharacter;
class TESObjectREFR;

namespace AimUtils {

// Player view control through FNV's clamped pitch, effective yaw and camera rotation path. Proven by
// FalloutNVAccess/src/AimUtil.cpp (MIT; notice preserved in src/defs/FalloutNVAccess-license.md).
float EffectiveYaw(PlayerCharacter *player);
void SetYaw(PlayerCharacter *player, float yaw);
float CameraPitch();
void SetPitch(PlayerCharacter *player, float pitch);

// Signed remaining view error in radians; positive heading turns right, positive pitch looks further down.
struct Error {
  float heading = 0.0f;
  float pitch = 0.0f;
};

// Turns the view toward a world point at a bounded rate and returns the error that remains afterwards.
Error AimAt(PlayerCharacter *player, const NiPoint3 &point, float deltaTime);

// Pitch that points the view from the player's eye at `point` (negative looks up, matching SetPitch).
float DesiredPitch(PlayerCharacter *player, const NiPoint3 &point);

using AimLogic::Purpose;

// The single answer to "where on this reference do I look or shoot", used by Walker and Combat. The height tables live
// in AimLogic.
NiPoint3 TargetPoint(TESObjectREFR *ref, Purpose purpose);

// Eye height above the player's feet for the current scale and stance (crouching). See AimLogic::kCrouchEyeRatio.
float EyeHeight(PlayerCharacter *player);
// The player's eye in world space (feet position raised by EyeHeight).
NiPoint3 EyePosition(PlayerCharacter *player);
bool IsCrouching(PlayerCharacter *player);

} // namespace AimUtils
