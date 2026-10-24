#include "Movement/AimLogic.hpp"
#include "TestFramework.hpp"
#include <cmath>
#include <limits>

using namespace AimLogic;

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kInf = std::numeric_limits<float>::infinity();
} // namespace

TEST("aim: WrapAngle keeps angles inside [-pi, pi]") {
  CHECK(std::abs(WrapAngle(0.5f) - 0.5f) < 1e-6f);
  CHECK(std::abs(WrapAngle(kPi + 0.5f) - (-kPi + 0.5f)) < 1e-5f);
  CHECK(std::abs(WrapAngle(-kPi - 0.5f) - (kPi - 0.5f)) < 1e-5f);
}

TEST("aim: WrapAngle handles huge multiples without looping") {
  const float wrapped = WrapAngle(1.0e7f);
  CHECK(std::isfinite(wrapped));
  CHECK(wrapped >= -kPi - 1e-4f && wrapped <= kPi + 1e-4f);
  CHECK(std::abs(WrapAngle(40.0f * kPi + 0.25f) - 0.25f) < 1e-3f);
}

TEST("aim: WrapAngle returns 0 for NaN and infinities instead of hanging") {
  CHECK_EQ(WrapAngle(kInf), 0.0f);
  CHECK_EQ(WrapAngle(-kInf), 0.0f);
  CHECK_EQ(WrapAngle(std::numeric_limits<float>::quiet_NaN()), 0.0f);
  CHECK_EQ(WrapDegrees(kInf), 0.0f);
  CHECK_EQ(WrapDegrees(std::numeric_limits<float>::quiet_NaN()), 0.0f);
}

TEST("aim: WrapDegrees keeps degrees inside [-180, 180]") {
  CHECK(std::abs(WrapDegrees(190.0f) - (-170.0f)) < 1e-3f);
  CHECK(std::abs(WrapDegrees(-190.0f) - 170.0f) < 1e-3f);
  CHECK(std::abs(WrapDegrees(720.0f + 30.0f) - 30.0f) < 1e-3f);
}

TEST("aim: the unified height table keeps the numbers Walker and Combat used") {
  CHECK_EQ(TargetHeight(TargetKind::Character, Purpose::Shoot), 70.0f);
  CHECK_EQ(TargetHeight(TargetKind::Creature, Purpose::Shoot), 35.0f);
  CHECK_EQ(TargetHeight(TargetKind::Object, Purpose::Shoot), 20.0f);
  CHECK_EQ(TargetHeight(TargetKind::Character, Purpose::Look), 100.0f);
  CHECK_EQ(TargetHeight(TargetKind::Creature, Purpose::Look), 100.0f);
  CHECK_EQ(TargetHeight(TargetKind::Activator, Purpose::Look), 50.0f);
  CHECK_EQ(TargetHeight(TargetKind::Flora, Purpose::Look), 20.0f);
  CHECK_EQ(TargetHeight(TargetKind::Object, Purpose::Look), 0.0f);
}

TEST("aim: target height scales with the reference and a corrupt scale is clamped") {
  CHECK_EQ(ScaledTargetHeight(TargetKind::Character, Purpose::Shoot, 1.0f), 70.0f);
  CHECK_EQ(ScaledTargetHeight(TargetKind::Character, Purpose::Shoot, 2.0f), 140.0f);
  CHECK(ScaledTargetHeight(TargetKind::Character, Purpose::Shoot, 1000.0f) <= 70.0f * 4.0f + 1e-3f);
  CHECK(ScaledTargetHeight(TargetKind::Character, Purpose::Shoot, 0.0f) > 0.0f);
  CHECK_EQ(ScaledTargetHeight(TargetKind::Character, Purpose::Shoot, std::numeric_limits<float>::quiet_NaN()), 70.0f);
}

TEST("aim: eye height follows scale and, once measured, stance") {
  CHECK_EQ(EyeHeight(1.0f, false), kStandingEyeHeight);
  CHECK_EQ(EyeHeight(1.1f, false), kStandingEyeHeight * 1.1f);
  CHECK_EQ(EyeHeight(1.0f, true), kStandingEyeHeight * kCrouchEyeRatio);
  CHECK(EyeHeight(1.0f, true) <= EyeHeight(1.0f, false));
}

TEST("aim: PitchTo is negative for a target above the eye and positive below") {
  CHECK(PitchTo(100.0f, 0.0f, 100.0f) < 0.0f);
  CHECK(PitchTo(100.0f, 0.0f, -100.0f) > 0.0f);
  CHECK(std::abs(PitchTo(100.0f, 0.0f, 0.0f)) < 1e-6f);
  CHECK(std::abs(PitchTo(0.0f, 100.0f, 100.0f) - PitchTo(100.0f, 0.0f, 100.0f)) < 1e-6f);
  CHECK(std::abs(PitchTo(100.0f, 0.0f, 100.0f) + kPi / 4.0f) < 1e-5f);
}
