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

using AimLogic::PointSource;
using AimLogic::Purpose;

// The single answer to "where on this reference do I look or shoot", used by Walker and Combat. Shooting an actor aims
// at its torso node (ActorTorsoPoint), then at its mesh bound centre; shooting a non-actor object aims at the centre
// of its mesh bound when that is usable (ObjectBoundCentre); everything else uses the height table in AimLogic.
// `source` (optional) receives where the point came from, for the Aim log line.
NiPoint3 TargetPoint(TESObjectREFR *ref, Purpose purpose, PointSource *source = nullptr);
// The world-space centre and radius of a reference's loaded 3D, when AimLogic::UsableObjectBound accepts it.
bool ObjectBoundCentre(TESObjectREFR *ref, NiPoint3 &centre, float &radius);
// The world position of an actor's chest (todo 56): the node its body part data names as the torso's VATS target, or
// the torso part's own node. False when the actor has no loaded 3D, no torso part, or neither node is in the skeleton
// within AimLogic::UsableBodyPoint of the actor.
bool ActorTorsoPoint(TESObjectREFR *ref, NiPoint3 &point, PointSource &source);

// The rendered world camera's position, where every aim and line-of-sight ray starts. False when it is not usable
// (no camera yet, or AimLogic::UsableCamera refuses it); there is no modelled fallback.
bool CameraPosition(PlayerCharacter *player, NiPoint3 &camera);
// PlayerCharacter::cameraPos, the camera position the player record keeps. Used before 2026-11-08; now only logged
// next to CameraPosition. Suspected (not shown) of going stale after a crouch or iron sights, when line of sight to
// clearly visible bottles started failing (todo 03); the logs print both so the two can be compared.
bool PlayerCameraField(PlayerCharacter *player, NiPoint3 &camera);
bool IsCrouching(PlayerCharacter *player);

} // namespace AimUtils
