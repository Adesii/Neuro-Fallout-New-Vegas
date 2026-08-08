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
