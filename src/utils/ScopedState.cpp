#include "ScopedState.hpp"

namespace ScopedState {

bool Delay::Ready(const void *owner, const std::string &signature, Clock::duration delay) {
  const auto now = Clock::now();
  if (m_owner != owner || m_signature != signature) {
    m_owner = owner;
    m_signature = signature;
    m_startedAt = now;
    return false;
  }
  return now - m_startedAt >= delay;
}

void Delay::Reset() {
  m_owner = nullptr;
  m_signature.clear();
}

void StepTimer::Start(Clock::time_point now) {
  m_started = true;
  m_wasRunning = true;
  m_last = now;
  m_elapsed = {};
}

void StepTimer::Tick(bool running, Clock::time_point now) {
  if (!m_started)
    return;
  const auto delta = now - m_last;
  m_last = now;
  // Both ends of the interval must be running: the first frame back after an obstruction adds nothing.
  if (running && m_wasRunning && delta <= kMaxStep)
    m_elapsed += delta;
  m_wasRunning = running;
}

void StepTimer::Reset() {
  m_started = false;
  m_wasRunning = false;
  m_elapsed = {};
}

bool Observation::IsCurrent(const void *owner, const std::string &signature) const {
  return m_owner == owner && m_signature == signature;
}

void Observation::Commit(const void *owner, const std::string &signature) {
  m_owner = owner;
  m_signature = signature;
}

void Observation::Reset() {
  m_owner = nullptr;
  m_signature.clear();
}

} // namespace ScopedState
