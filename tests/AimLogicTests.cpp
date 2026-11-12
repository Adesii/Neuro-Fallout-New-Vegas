#include "Movement/AimLogic.hpp"
#include "TestFramework.hpp"
#include <cmath>
#include <iterator>
#include <limits>
#include <string>

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

TEST("aim: the height table keeps Walker's look heights and has no guessed shoot height for an actor") {
  // An actor is shot at its torso node (todo 56); the old 70 / 35 landed low and are gone, with no fallback.
  CHECK_EQ(TargetHeight(TargetKind::Character, Purpose::Shoot), 0.0f);
  CHECK_EQ(TargetHeight(TargetKind::Creature, Purpose::Shoot), 0.0f);
  CHECK_EQ(TargetHeight(TargetKind::Object, Purpose::Shoot), 20.0f);
  CHECK_EQ(TargetHeight(TargetKind::Character, Purpose::Look), 100.0f);
  CHECK_EQ(TargetHeight(TargetKind::Creature, Purpose::Look), 100.0f);
  CHECK_EQ(TargetHeight(TargetKind::Activator, Purpose::Look), 50.0f);
  CHECK_EQ(TargetHeight(TargetKind::Flora, Purpose::Look), 20.0f);
  CHECK_EQ(TargetHeight(TargetKind::Object, Purpose::Look), 0.0f);
}

TEST("aim: target height scales with the reference and a corrupt scale is clamped") {
  CHECK_EQ(ScaledTargetHeight(TargetKind::Character, Purpose::Look, 1.0f), 100.0f);
  CHECK_EQ(ScaledTargetHeight(TargetKind::Character, Purpose::Look, 2.0f), 200.0f);
  CHECK(ScaledTargetHeight(TargetKind::Character, Purpose::Look, 1000.0f) <= 100.0f * 4.0f + 1e-3f);
  CHECK(ScaledTargetHeight(TargetKind::Character, Purpose::Look, 0.0f) > 0.0f);
  CHECK_EQ(ScaledTargetHeight(TargetKind::Character, Purpose::Look, std::numeric_limits<float>::quiet_NaN()), 100.0f);
  CHECK_EQ(ScaledTargetHeight(TargetKind::Object, Purpose::Shoot, 2.0f), 40.0f);
}

TEST("aim: PitchTo is negative for a target above the eye and positive below") {
  CHECK(PitchTo(100.0f, 0.0f, 100.0f) < 0.0f);
  CHECK(PitchTo(100.0f, 0.0f, -100.0f) > 0.0f);
  CHECK(std::abs(PitchTo(100.0f, 0.0f, 0.0f)) < 1e-6f);
  CHECK(std::abs(PitchTo(0.0f, 100.0f, 100.0f) - PitchTo(100.0f, 0.0f, 100.0f)) < 1e-6f);
  CHECK(std::abs(PitchTo(100.0f, 0.0f, 100.0f) + kPi / 4.0f) < 1e-5f);
}

TEST("aim: any finite camera position is used, a non-finite one never") {
  CHECK(AimLogic::UsableCamera(-68303.5f, 5765.0f, 8563.0f));
  CHECK(AimLogic::UsableCamera(0.0f, 0.0f, 0.0f));
  CHECK(!AimLogic::UsableCamera(std::numeric_limits<float>::quiet_NaN(), 0.0f, 100.0f));
  CHECK(!AimLogic::UsableCamera(0.0f, std::numeric_limits<float>::infinity(), 100.0f));
  CHECK(!AimLogic::UsableCamera(0.0f, 0.0f, -std::numeric_limits<float>::infinity()));
}

TEST("aim: an object's bound is used only when it belongs to the object") {
  CHECK(AimLogic::UsableObjectBound(12.0f, 10.0f));  // a bottle: centre a little above its base
  CHECK(AimLogic::UsableObjectBound(12.0f, 40.0f));  // within radius plus slack
  CHECK(!AimLogic::UsableObjectBound(12.0f, 80.0f)); // the centre of something else
  CHECK(!AimLogic::UsableObjectBound(0.0f, 0.0f));
  CHECK(!AimLogic::UsableObjectBound(-3.0f, 0.0f));
  CHECK(!AimLogic::UsableObjectBound(5000.0f, 0.0f)); // a whole-cell or parent bound
  CHECK(!AimLogic::UsableObjectBound(std::numeric_limits<float>::quiet_NaN(), 0.0f));
}

TEST("aim: a skeleton node is an aim point only while it is on the actor") {
  CHECK(AimLogic::UsableBodyPoint(2.0f, -3.0f, 95.0f, 1.0f));    // a human chest
  CHECK(AimLogic::UsableBodyPoint(10.0f, 0.0f, 30.0f, 0.6f));    // a small gecko
  CHECK(AimLogic::UsableBodyPoint(0.0f, 0.0f, 0.0f, 1.0f));      // a root node at the origin is still the actor
  CHECK(!AimLogic::UsableBodyPoint(4000.0f, 0.0f, 90.0f, 1.0f)); // a skeleton left behind where the actor was
  CHECK(AimLogic::UsableBodyPoint(0.0f, 0.0f, 500.0f, 2.0f));    // a large, scaled-up creature
  CHECK(!AimLogic::UsableBodyPoint(0.0f, 0.0f, 500.0f, 1.0f));
  CHECK(!AimLogic::UsableBodyPoint(std::numeric_limits<float>::quiet_NaN(), 0.0f, 90.0f, 1.0f));
  CHECK(!AimLogic::UsableBodyPoint(0.0f, std::numeric_limits<float>::infinity(), 90.0f, 1.0f));
  // A corrupt scale falls back to 1 instead of accepting or refusing everything.
  CHECK(AimLogic::UsableBodyPoint(0.0f, 0.0f, 95.0f, std::numeric_limits<float>::quiet_NaN()));
  CHECK(!AimLogic::UsableBodyPoint(0.0f, 0.0f, 500.0f, std::numeric_limits<float>::quiet_NaN()));
}

TEST("aim: every aim point source has its own name for the log, and the origin stands out") {
  const PointSource sources[] = {PointSource::TorsoTarget, PointSource::TorsoNode,   PointSource::MeshBound,
                                 PointSource::ObjectBound, PointSource::HeightTable, PointSource::Origin};
  for (size_t left = 0; left < std::size(sources); ++left) {
    CHECK(std::string(PointSourceName(sources[left])) != "unknown");
    for (size_t right = left + 1; right < std::size(sources); ++right)
      CHECK(std::string(PointSourceName(sources[left])) != PointSourceName(sources[right]));
  }
  CHECK(std::string(PointSourceName(PointSource::Origin)).find("ORIGIN") != std::string::npos);
}
