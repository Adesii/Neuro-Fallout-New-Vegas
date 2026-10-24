#include "AimUtils.hpp"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "Movement/StanceLogic.hpp"
#include "utils/math.h"
#include <algorithm>
#include <cmath>

namespace AimUtils {
namespace {

using SetPitchWithClamp = void(__thiscall *)(Actor *, float);
using AddDeltaYaw = void(__thiscall *)(Actor *, float);
using GetEffectiveYaw = float(__thiscall *)(PlayerCharacter *, int);
const auto kSetPitchWithClamp = reinterpret_cast<SetPitchWithClamp>(0x931D90);
const auto kAddDeltaYaw = reinterpret_cast<AddDeltaYaw>(0x931D30);
const auto kGetEffectiveYaw = reinterpret_cast<GetEffectiveYaw>(0x953F20);
auto *const kCameraPitch = reinterpret_cast<float *>(0x11E0764);
auto *const kCameraYaw = reinterpret_cast<float *>(0x11E076C);

constexpr float kPitchLimit = 1.5533f;
// Combat's snap-to-target turn. Walker has its own, slower, smoothed turn (kWalkTurnRate in WalkerHandler.cpp).
constexpr float kCombatTurnResponse = 10.0f;
constexpr float kCombatTurnRate = 7.0f;
constexpr float kPitchResponse = 12.0f;

using AimLogic::WrapAngle;

AimLogic::TargetKind ClassifyTarget(TESObjectREFR *ref) {
  if (ref->IsActor())
    return ref->baseForm && ref->baseForm->eFormType == _FormType::TESCreature ? AimLogic::TargetKind::Creature
                                                                               : AimLogic::TargetKind::Character;
  if (!ref->baseForm)
    return AimLogic::TargetKind::Object;
  switch (ref->baseForm->eFormType) {
  case _FormType::TESObjectACTI:
  case _FormType::BGSTalkingActivator:
  case _FormType::TESObjectDOOR:
  case _FormType::TESObjectCONT:
  case _FormType::BGSTerminal:
  case _FormType::TESFurniture:
    return AimLogic::TargetKind::Activator;
  case _FormType::TESFlora:
    return AimLogic::TargetKind::Flora;
  default:
    return AimLogic::TargetKind::Object;
  }
}

} // namespace

float EffectiveYaw(PlayerCharacter *player) { return kGetEffectiveYaw(player, 0); }

void SetYaw(PlayerCharacter *player, float yaw) {
  yaw = WrapAngle(yaw);
  const float delta = WrapAngle(yaw - EffectiveYaw(player));
  kAddDeltaYaw(player, delta);
  player->rot.z = yaw;
  *kCameraYaw = yaw;
}

float CameraPitch() { return *kCameraPitch; }

void SetPitch(PlayerCharacter *player, float pitch) {
  if (!player)
    return;
  pitch = std::clamp(pitch, -kPitchLimit, kPitchLimit);
  kSetPitchWithClamp(player, pitch);
  player->rot.x = pitch;
  *kCameraPitch = pitch;
}

bool IsCrouching(PlayerCharacter *player) {
  return player && player->actorMover && Stance::IsSneakingFlags(player->GetMovementFlags());
}

float EyeHeight(PlayerCharacter *player) { return AimLogic::EyeHeight(player->GetScale(), IsCrouching(player)); }

NiPoint3 EyePosition(PlayerCharacter *player) {
  NiPoint3 eye = player->GetPos();
  eye.z += EyeHeight(player);
  return eye;
}

NiPoint3 TargetPoint(TESObjectREFR *ref, Purpose purpose) {
  NiPoint3 point = ref->GetPos();
  point.z += AimLogic::ScaledTargetHeight(ClassifyTarget(ref), purpose, ref->GetScale());
  return point;
}

float DesiredPitch(PlayerCharacter *player, const NiPoint3 &point) {
  const NiPoint3 eye = EyePosition(player);
  return AimLogic::PitchTo(point.x - eye.x, point.y - eye.y, point.z - eye.z);
}

Error AimAt(PlayerCharacter *player, const NiPoint3 &point, float deltaTime) {
  Error error;
  if (!player)
    return error;

  const float currentYaw = EffectiveYaw(player);
  float heading =
      Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(currentYaw), point.x, point.y);
  if (deltaTime > 0.0f) {
    float turn = std::clamp(heading * kCombatTurnResponse, -kCombatTurnRate, kCombatTurnRate) * deltaTime;
    if (std::abs(turn) > std::abs(heading))
      turn = heading;
    SetYaw(player, currentYaw + turn);
    heading -= turn;
  }
  error.heading = heading;

  const float desiredPitch = DesiredPitch(player, point);
  if (deltaTime > 0.0f) {
    const float alpha = 1.0f - std::exp(-kPitchResponse * deltaTime);
    SetPitch(player, CameraPitch() + (desiredPitch - CameraPitch()) * alpha);
  }
  error.pitch = desiredPitch - CameraPitch();
  return error;
}

} // namespace AimUtils
