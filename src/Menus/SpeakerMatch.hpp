#pragma once

#include <string>
#include <vector>

namespace Menus::SpeakerMatch {

struct Point {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

// An actor that could have spoken: its name and the vertical line segment it occupies (feet at `base`, `height` above).
struct Candidate {
  std::string name;
  Point base;
  float height = 0.0f;
};

// The subtitle's source position can be anywhere on the speaker (feet, head, mouth), so distance is measured to the
// actor's body segment, not to a single point.
constexpr float kDefaultBodyHeight = 160.0f;
// A source farther than this from every body is not any of these actors (radio, narrator, a speaker not loaded).
constexpr float kMaxDistance = 80.0f;
// The runner-up must be at least this much farther than the best match, otherwise the speaker is ambiguous and no
// name is given: a missing name is better than a wrong one.
constexpr float kAmbiguityMargin = 40.0f;

enum class Outcome { Matched, NoCandidate, TooFar, Ambiguous, Unnamed };

struct Result {
  Outcome outcome = Outcome::NoCandidate;
  std::string name;      // set only when Matched
  float distance = 0.0f; // to the best candidate (when any)
  float runnerUp = 0.0f; // to the second best (when any)
};

float DistanceToBody(const Point &source, const Candidate &candidate);
Result Resolve(const Point &source, const std::vector<Candidate> &candidates);
const char *OutcomeName(Outcome outcome);

} // namespace Menus::SpeakerMatch
