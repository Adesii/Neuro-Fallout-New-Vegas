#pragma once

#include "Action.hpp"
#include <memory>
#include <vector>

namespace Actions {

class PersistentActionSet {
public:
  PersistentActionSet &Add(std::unique_ptr<IAction> action);
  bool Register();
  void Abandon();
  bool IsRegistered() const;

private:
  std::vector<std::unique_ptr<IAction>> m_actions;
  bool m_registered = false;
};

} // namespace Actions
