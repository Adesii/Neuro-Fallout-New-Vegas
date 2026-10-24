#pragma once

#include "Action.hpp"
#include "utils/ScopedState.hpp"
#include <memory>
#include <vector>

namespace Actions {

class PersistentActionSet {
public:
  PersistentActionSet &Add(std::unique_ptr<IAction> action);
  // A failed registration is not retried (or logged) again for kRetryDelay. The delay is cleared when the registry is
  // reset (reconnect, load) so the set is restored at once. `now` is a test seam.
  bool Register(ScopedState::Clock::time_point now = ScopedState::Clock::now());
  bool Unregister();
  // Registers when `wanted`, otherwise unregisters: the per-frame "should this set be published now" tail of a handler.
  void Sync(bool wanted) {
    if (wanted)
      Register();
    else
      Unregister();
  }
  // No action was added yet: handlers build their sets lazily on first use instead of keeping a "built" flag.
  bool Empty() const { return m_actions.empty(); }

  static constexpr auto kRetryDelay = std::chrono::seconds(2);

private:
  std::vector<std::unique_ptr<IAction>> m_actions;
  enum class State { Unregistered, Registered, Closing };
  State m_state = State::Unregistered;
  ScopedState::Backoff m_retry{kRetryDelay};
  uint64_t m_retryGeneration = 0;
};

} // namespace Actions
