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

// Pitch that points the view from the camera at `point` (negative looks up, matching SetPitch). Without a usable
// camera it returns the current pitch, so the view holds still.
float DesiredPitch(PlayerCharacter *player, const NiPoint3 &point);

using AimLogic::Purpose;

// The single answer to "where on this reference do I look or shoot", used by Walker and Combat. Shooting a non-actor
// object aims at the centre of its mesh bound when that is usable (ObjectBoundCentre); everything else uses the height
// tables in AimLogic.
NiPoint3 TargetPoint(TESObjectREFR *ref, Purpose purpose);
// The world-space centre and radius of a reference's loaded 3D, when AimLogic::UsableObjectBound accepts it.
bool ObjectBoundCentre(TESObjectREFR *ref, NiPoint3 &centre, float &radius);

// The engine's camera position (PlayerCharacter::cameraPos), where every aim and line-of-sight ray starts. False when
// it is not usable (AimLogic::UsableCamera); there is no modelled fallback.
bool CameraPosition(PlayerCharacter *player, NiPoint3 &camera);
bool IsCrouching(PlayerCharacter *player);

} // namespace AimUtils
