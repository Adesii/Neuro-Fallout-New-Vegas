#include "AimLogic.hpp"
#include <algorithm>
#include <cmath>

namespace AimLogic {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinScale = 0.3f;
constexpr float kMaxScale = 4.0f;

float ClampScale(float scale) { return std::isfinite(scale) ? std::clamp(scale, kMinScale, kMaxScale) : 1.0f; }

} // namespace

float WrapAngle(float radians) {
  if (!std::isfinite(radians))
    return 0.0f;
  return std::remainder(radians, kPi * 2.0f);
}

float WrapDegrees(float degrees) {
  if (!std::isfinite(degrees))
    return 0.0f;
  return std::remainder(degrees, 360.0f);
}

float TargetHeight(TargetKind kind, Purpose purpose) {
  if (purpose == Purpose::Look) {
    switch (kind) {
    case TargetKind::Character:
    case TargetKind::Creature:
      return 100.0f; // Walker looked at head height for any actor
    case TargetKind::Activator:
      return 50.0f;
    case TargetKind::Flora:
      return 20.0f;
    case TargetKind::Object:
      return 0.0f;
    }
    return 0.0f;
  }
  switch (kind) {
  case TargetKind::Character:
    return 70.0f;
  case TargetKind::Creature:
    return 35.0f;
  default:
    return 20.0f; // Combat's free-aim height for everything that is not an actor
  }
}

float ScaledTargetHeight(TargetKind kind, Purpose purpose, float scale) {
  return TargetHeight(kind, purpose) * ClampScale(scale);
}

float EyeHeight(float scale, bool crouching) {
  return kStandingEyeHeight * ClampScale(scale) * (crouching ? kCrouchEyeRatio : 1.0f);
}

bool PlausibleCameraOffset(float dx, float dy, float dz) {
  if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz))
    return false;
  return std::sqrt(dx * dx + dy * dy + dz * dz) <= kMaxCameraOffset;
}

bool UsableObjectBound(float radius, float centreToOrigin) {
  if (!std::isfinite(radius) || !std::isfinite(centreToOrigin))
    return false;
  return radius > 0.0f && radius <= kMaxObjectBoundRadius && centreToOrigin <= radius + kBoundSlack;
}

float PitchTo(float dx, float dy, float dz) { return -std::atan2(dz, std::sqrt(dx * dx + dy * dy)); }

} // namespace AimLogic
