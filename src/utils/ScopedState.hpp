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

// Spaces out retries of an operation that fails persistently (and would otherwise log every frame).
class Backoff {
public:
  explicit Backoff(Clock::duration delay) : m_delay(delay) {}
  bool Ready(Clock::time_point now = Clock::now()) const { return now >= m_retryAt; }
  void Fail(Clock::time_point now = Clock::now()) { m_retryAt = now + m_delay; }
  void Reset() { m_retryAt = {}; }

private:
  Clock::duration m_delay;
  Clock::time_point m_retryAt{};
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
