#include "Menus/SpeakerMatch.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Menus::SpeakerMatch {

float DistanceToBody(const Point &source, const Candidate &candidate) {
  const float dx = source.x - candidate.base.x;
  const float dy = source.y - candidate.base.y;
  const float height = std::max(candidate.height, 0.0f);
  const float dz = source.z - std::clamp(source.z - candidate.base.z, 0.0f, height) - candidate.base.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Result Resolve(const Point &source, const std::vector<Candidate> &candidates) {
  Result result;
  if (!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.z))
    return result;

  const Candidate *best = nullptr;
  float bestDistance = std::numeric_limits<float>::infinity();
  float runnerUp = std::numeric_limits<float>::infinity();
  for (const auto &candidate : candidates) {
    const float distance = DistanceToBody(source, candidate);
    if (!std::isfinite(distance))
      continue;
    if (distance < bestDistance) {
      runnerUp = bestDistance;
      bestDistance = distance;
      best = &candidate;
    } else if (distance < runnerUp) {
      runnerUp = distance;
    }
  }
  if (!best)
    return result;

  result.distance = bestDistance;
  result.runnerUp = std::isfinite(runnerUp) ? runnerUp : 0.0f;
  if (bestDistance > kMaxDistance) {
    result.outcome = Outcome::TooFar;
  } else if (std::isfinite(runnerUp) && runnerUp - bestDistance < kAmbiguityMargin) {
    result.outcome = Outcome::Ambiguous;
  } else if (best->name.empty()) {
    result.outcome = Outcome::Unnamed;
  } else {
    result.outcome = Outcome::Matched;
    result.name = best->name;
  }
  return result;
}

const char *OutcomeName(Outcome outcome) {
  switch (outcome) {
  case Outcome::Matched:
    return "matched";
  case Outcome::NoCandidate:
    return "no candidate";
  case Outcome::TooFar:
    return "too far from every actor";
  case Outcome::Ambiguous:
    return "ambiguous";
  case Outcome::Unnamed:
    return "unnamed actor";
  }
  return "unknown";
}

} // namespace Menus::SpeakerMatch
