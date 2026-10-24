#include "WalkerLogic.hpp"
#include "Context/Distance.hpp"
#include <algorithm>
#include <cmath>

namespace WalkerLogic {

// --- Messages ---------------------------------------------------------------------------------------------------

std::string DescribeFailure(FailureReason reason, const std::string &target, const std::string &detail) {
  switch (reason) {
  case FailureReason::Stuck:
    return "Could not reach " + target +
           ": you got stuck and nothing you tried (jumping, stepping aside, finding another way) freed you. Try "
           "another route or a different target.";
  case FailureReason::DoorLocked:
    return "Could not reach " + target + ": " + (detail.empty() ? std::string("a door") : detail) +
           " is locked and in your way.";
  case FailureReason::NoProgress:
    return "Could not reach " + target +
           ": you kept walking but never got meaningfully closer, so it may not be "
           "reachable from here.";
  case FailureReason::TargetGone:
    return target + " is no longer available.";
  case FailureReason::WrongFloor:
    return "Could not reach " + target +
           ": it is close in a straight line but not reachable from where you stand "
           "(another floor, or something in between). Find a way around.";
  case FailureReason::ActivationRejected:
    return "The game rejected interaction with " + target + ".";
  case FailureReason::NotInteractable:
    return target + " is not interactable.";
  case FailureReason::NoWaypoint:
    return "Could not create a navigation waypoint.";
  case FailureReason::Generic:
    break;
  }
  return "Could not reach " + target + ".";
}

std::string DescribePause(const std::string &target, const std::string &cause) {
  return "Your walk to " + target + " is on hold: " + cause + ". It continues by itself once that is over.";
}

std::string DescribeProgress(bool quest, bool exploring, const std::string &target, float walkedDistance,
                             float walkSeconds, float remainingDistance) {
  std::string text = quest       ? "You are still following the quest route: "
                     : exploring ? "You are still exploring toward "
                                 : "You are still walking to ";
  text += target + ". Distance walked: " + Units::FormatDistance(walkedDistance) +
          ". Walk time: " + std::to_string(static_cast<long>(std::lround(walkSeconds))) +
          " s. Distance to target: " + Units::FormatDistance(remainingDistance) + ".";
  return text;
}

// --- Stuck detection --------------------------------------------------------------------------------------------

void StuckDetector::Reset() {
  m_samples.clear();
  m_clock = 0.0f;
  m_grace = 0.0f;
}

void StuckDetector::StartGrace(float seconds) {
  m_samples.clear();
  m_grace = std::max(m_grace, seconds);
}

bool StuckDetector::Update(float dt, float x, float y, bool moving) {
  m_clock += dt;
  if (m_grace > 0.0f) {
    m_grace -= dt;
    m_samples.clear();
    return false;
  }
  if (!moving) {
    m_samples.clear();
    return false;
  }
  if (!m_samples.empty()) {
    const Sample &last = m_samples.back();
    if (std::hypot(x - last.x, y - last.y) > kTeleportDistance) {
      m_samples.clear();
      return false;
    }
  }
  m_samples.push_back({m_clock, x, y});
  while (m_samples.size() > 1 && m_samples[1].time <= m_clock - kWindowSeconds)
    m_samples.pop_front();

  const Sample &first = m_samples.front();
  if (m_clock - first.time < kWindowSeconds)
    return false;
  const bool stuck = std::hypot(x - first.x, y - first.y) < kMinDistance;
  if (stuck)
    m_samples.clear();
  return stuck;
}

void OscillationDetector::Reset() {
  m_last = 0.0f;
  m_window = 0.0f;
  m_minimum = 0.0f;
  m_maximum = 0.0f;
  m_direction = 0;
  m_reversals = 0;
  m_initialized = false;
}

bool OscillationDetector::Update(float dt, float targetDistance) {
  if (!std::isfinite(targetDistance)) {
    Reset();
    return false;
  }
  if (!m_initialized) {
    m_last = m_minimum = m_maximum = targetDistance;
    m_initialized = true;
    return false;
  }

  m_window += dt;
  m_minimum = std::min(m_minimum, targetDistance);
  m_maximum = std::max(m_maximum, targetDistance);
  const float delta = targetDistance - m_last;
  const int direction = delta >= kDirectionThreshold ? 1 : delta <= -kDirectionThreshold ? -1 : 0;
  if (direction) {
    if (m_direction && direction != m_direction)
      ++m_reversals;
    m_direction = direction;
    m_last = targetDistance;
  }

  const float range = m_maximum - m_minimum;
  if (m_reversals >= kReversalLimit && range <= kDistanceMargin) {
    Reset();
    return true;
  }
  if (m_window >= kWindowSeconds || range > kDistanceMargin)
    Reset();
  return false;
}

void ProgressTracker::Reset() {
  m_best = 0.0f;
  m_idle = 0.0f;
  m_initialized = false;
}

bool ProgressTracker::Update(float dt, float remaining, bool counting) {
  if (!counting || !std::isfinite(remaining)) {
    Reset();
    return false;
  }
  if (!m_initialized) {
    m_best = remaining;
    m_idle = 0.0f;
    m_initialized = true;
    return false;
  }
  if (remaining < m_best - kMinImprovement) {
    m_best = remaining;
    m_idle = 0.0f;
    return false;
  }
  m_idle += dt;
  if (m_idle < kStallSeconds)
    return false;
  Reset();
  return true;
}

// --- Recovery ladder --------------------------------------------------------------------------------------------

float StepDuration(RecoveryStep step) {
  switch (step) {
  case RecoveryStep::Door:
    return 2.5f;
  case RecoveryStep::Jump:
    return 1.2f;
  case RecoveryStep::Sidestep:
    return 0.8f;
  case RecoveryStep::BackOff:
    return 0.7f;
  case RecoveryStep::Wander:
    return 8.0f;
  }
  return 1.0f;
}

void DetourOffset(int tier, float random01, float &dx, float &dy) {
  const float radius = 220.0f + 130.0f * static_cast<float>(std::clamp(tier, 0, 3));
  const float angle = std::clamp(random01, 0.0f, 1.0f) * 2.0f * 3.14159265358979f;
  dx = std::cos(angle) * radius;
  dy = std::sin(angle) * radius;
}

void RecoveryPlanner::Reset(int budget) {
  m_budget = std::max(budget, 0);
  m_attempts = 0;
  m_ladder = 0;
  m_doorTried = false;
  m_clean = 0.0f;
  m_spots.clear();
}

std::optional<RecoveryPlanner::Choice> RecoveryPlanner::Next(bool canJump, bool doorAvailable, float x, float y) {
  if (m_attempts >= m_budget)
    return std::nullopt;
  ++m_attempts;
  m_clean = 0.0f;
  m_spots.push_back({x, y});
  if (m_spots.size() > 16)
    m_spots.erase(m_spots.begin());

  const int remaining = m_budget - m_attempts;
  if (remaining < kWanderAttempts && m_budget > kWanderAttempts)
    return Choice{RecoveryStep::Wander, false, kWanderAttempts - 1 - remaining};
  if (doorAvailable && !m_doorTried) {
    m_doorTried = true;
    return Choice{RecoveryStep::Door, false, 0};
  }

  constexpr int kLadderSize = 4;
  for (;;) {
    switch (m_ladder++ % kLadderSize) {
    case 0:
      if (canJump)
        return Choice{RecoveryStep::Jump, false, 0};
      break;
    case 1:
      return Choice{RecoveryStep::Sidestep, false, 0};
    case 2:
      return Choice{RecoveryStep::Sidestep, true, 0};
    default:
      return Choice{RecoveryStep::BackOff, false, 0};
    }
  }
}

void RecoveryPlanner::Update(float dt, float x, float y) {
  if (m_attempts == 0)
    return;
  for (const Spot &spot : m_spots) {
    if (std::hypot(x - spot.x, y - spot.y) < kProgressDistance) {
      m_clean = 0.0f;
      return;
    }
  }
  m_clean += dt;
  if (m_clean < kCleanSeconds)
    return;
  m_attempts = 0;
  m_ladder = 0;
  m_doorTried = false;
  m_clean = 0.0f;
  m_spots.clear();
}

// --- Reminder and pause -----------------------------------------------------------------------------------------

void ProgressReminder::Reset() {
  m_sinceReminder = 0.0f;
  m_walkedTotal = 0.0f;
  m_secondsTotal = 0.0f;
}

std::optional<ProgressReminder::Snapshot> ProgressReminder::Update(float dt, float walked, bool exploring) {
  m_secondsTotal += dt;
  m_sinceReminder += dt;
  if (walked > 0.0f)
    m_walkedTotal += walked;
  if (m_sinceReminder < (exploring ? kExploreInterval : kInterval))
    return std::nullopt;
  m_sinceReminder = 0.0f;
  return Snapshot{m_walkedTotal, m_secondsTotal};
}

void PauseNotice::Reset() {
  m_paused = 0.0f;
  m_sent = false;
}

bool PauseNotice::Update(float dt, bool paused) {
  if (!paused) {
    Reset();
    return false;
  }
  m_paused += dt;
  if (m_sent || m_paused < kAfterSeconds)
    return false;
  m_sent = true;
  return true;
}

} // namespace WalkerLogic
