#pragma once

#include <chrono>
#include <string>

namespace ScopedState {

using Clock = std::chrono::steady_clock;

class Delay {
public:
  bool Ready(const void *owner, const std::string &signature, Clock::duration delay);
  void Reset();

private:
  const void *m_owner = nullptr;
  std::string m_signature;
  Clock::time_point m_startedAt;
};

class Observation {
public:
  bool IsCurrent(const void *owner, const std::string &signature) const;
  void Commit(const void *owner, const std::string &signature);
  void Reset();

private:
  const void *m_owner = nullptr;
  std::string m_signature;
};

} // namespace ScopedState
