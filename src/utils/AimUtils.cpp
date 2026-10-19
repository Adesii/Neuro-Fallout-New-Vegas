#include "AimUtils.hpp"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
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
constexpr float kTurnResponse = 10.0f;
constexpr float kMaxTurnRate = 7.0f;
constexpr float kPitchResponse = 12.0f;

float WrapAngle(float angle) {
  const float pi = static_cast<float>(Math::PI);
  while (angle > pi)
    angle -= pi * 2.0f;
  while (angle < -pi)
    angle += pi * 2.0f;
  return angle;
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

float DesiredPitch(const PlayerCharacter *player, const NiPoint3 &point) {
  const float x = point.x - player->pos.x;
  const float y = point.y - player->pos.y;
  const float z = point.z - (player->pos.z + kEyeHeight);
  return -std::atan2(z, std::sqrt(x * x + y * y));
}

Error AimAt(PlayerCharacter *player, const NiPoint3 &point, float deltaTime) {
  Error error;
  if (!player)
    return error;

  const float currentYaw = EffectiveYaw(player);
  float heading =
      Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(currentYaw), point.x, point.y);
  if (deltaTime > 0.0f) {
    float turn = std::clamp(heading * kTurnResponse, -kMaxTurnRate, kMaxTurnRate) * deltaTime;
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
