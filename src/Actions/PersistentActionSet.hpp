#pragma once

#include "Action.hpp"
#include <memory>
#include <vector>

namespace Actions {

class PersistentActionSet {
public:
  PersistentActionSet &Add(std::unique_ptr<IAction> action);
  bool Register();
  bool Unregister();

private:
  std::vector<std::unique_ptr<IAction>> m_actions;
  enum class State { Unregistered, Registered, Closing };
  State m_state = State::Unregistered;
};

} // namespace Actions
