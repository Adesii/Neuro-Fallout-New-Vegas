#pragma once

class NiPoint3;
class PlayerCharacter;

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
float DesiredPitch(const PlayerCharacter *player, const NiPoint3 &point);

constexpr float kEyeHeight = 100.0f;

} // namespace AimUtils
