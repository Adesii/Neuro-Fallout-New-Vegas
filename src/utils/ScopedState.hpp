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

// A timeout for a multi-step menu operation that only counts time while the operation can actually progress. Tick()
// every frame with whether the operation is running (its menu is on top and nothing obstructs it); time spent covered
// by a popup, a loading screen or a long frame does not count, so resuming never times out immediately.
class StepTimer {
public:
  // A frame longer than this is treated as a stall (load, alt-tab) and not counted.
  static constexpr auto kMaxStep = std::chrono::seconds(1);

  void Start(Clock::time_point now = Clock::now());
  void Tick(bool running, Clock::time_point now = Clock::now());
  bool Started() const { return m_started; }
  Clock::duration Elapsed() const { return m_elapsed; }
  bool Expired(Clock::duration limit) const { return m_started && m_elapsed > limit; }
  void Reset();

private:
  bool m_started = false;
  bool m_wasRunning = false;
  Clock::time_point m_last{};
  Clock::duration m_elapsed{};
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
