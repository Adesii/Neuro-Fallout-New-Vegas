#include "Menus/SpeakerMatch.hpp"
#include "Menus/SubtitleTracker.hpp"
#include "TestFramework.hpp"

#include <limits>

using namespace Menus::SpeakerMatch;

namespace {
Candidate Actor(const char *name, float x, float y, float z = 0.0f) { return {name, {x, y, z}, kDefaultBodyHeight}; }
} // namespace

TEST("speaker: distance is measured to the body, so feet, mouth and head positions all match") {
  const auto actor = Actor("Doc Mitchell", 100, 200, 50);
  CHECK_EQ(DistanceToBody({100, 200, 50}, actor), 0.0f);             // feet
  CHECK_EQ(DistanceToBody({100, 200, 50 + 150}, actor), 0.0f);       // head height
  CHECK_EQ(DistanceToBody({130, 200, 50 + 100}, actor), 30.0f);      // beside the torso
  CHECK_EQ(DistanceToBody({100, 200, 50 - 20}, actor), 20.0f);       // below the feet
  CHECK_EQ(DistanceToBody({100, 200, 50 + 160 + 15}, actor), 15.0f); // above the head
}

TEST("speaker: the actor the source lies on is the speaker") {
  const auto result = Resolve({100, 200, 130}, {Actor("Sunny Smiles", 90, 205), Actor("Trudy", 400, 200)});
  CHECK(result.outcome == Outcome::Matched);
  CHECK(result.name == "Sunny Smiles");
}

TEST("speaker: neighbours standing close together are told apart when the source is clear") {
  // 60 units apart: the source is on one of them and 55 from the other.
  const auto result = Resolve({100, 200, 100}, {Actor("Left", 100, 200), Actor("Right", 100, 255)});
  CHECK(result.outcome == Outcome::Matched);
  CHECK(result.name == "Left");
}

TEST("speaker: two plausible actors give no name instead of a guess") {
  const auto result = Resolve({130, 200, 100}, {Actor("Left", 100, 200), Actor("Right", 160, 200)});
  CHECK(result.outcome == Outcome::Ambiguous);
  CHECK(result.name.empty());
}

TEST("speaker: a source far from every actor (radio, unloaded speaker) gives no name") {
  const auto result = Resolve({5000, 5000, 0}, {Actor("Nearby", 0, 0)});
  CHECK(result.outcome == Outcome::TooFar);
  CHECK(result.name.empty());
}

TEST("speaker: an unnamed best match gives no name, and no candidates or a bad position are handled") {
  CHECK(Resolve({0, 0, 0}, {Actor("", 0, 0)}).outcome == Outcome::Unnamed);
  CHECK(Resolve({0, 0, 0}, {}).outcome == Outcome::NoCandidate);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK(Resolve({nan, 0, 0}, {Actor("A", 0, 0)}).outcome == Outcome::NoCandidate);
}

TEST("speaker: a lone actor in range is enough, and taller actors reach higher") {
  const auto lone = Resolve({0, 0, 40}, {Actor("Only", 0, 0)});
  CHECK(lone.outcome == Outcome::Matched);
  Candidate giant{"Super Mutant", {0, 0, 0}, kDefaultBodyHeight * 2.0f};
  CHECK_EQ(DistanceToBody({0, 0, 300}, giant), 0.0f);
}

TEST("subtitle identity ignores the moving source position and the resolved speaker") {
  Menus::SubtitleEntry a{"Hello", 5, 1, 1, 2, 3, "Doc"};
  Menus::SubtitleEntry b{"Hello", 5, 1, 9, 9, 9, ""};
  CHECK(a == b);
  CHECK(!(a == Menus::SubtitleEntry{"Hello", 6, 1}));
}
