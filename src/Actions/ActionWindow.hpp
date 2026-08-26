#pragma once

#include "Action.hpp"
#include "NeuroSDK.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Actions {

class ActionWindow {
public:
  enum class State { Building, Registered, Forced, Closing, Ended, Faulted };

  ActionWindow &Add(std::unique_ptr<IAction> action);
  ActionWindow &SetForce(std::string query, std::string state,
                         NeuroSDK::ActionPriority priority = NeuroSDK::ActionPriority::High,
                         bool ephemeralContext = true);

  bool Register();
  bool End();
  void Abandon();
  State GetState() const;
  const std::vector<std::unique_ptr<IAction>> &GetActions() const;

private:
  State m_state = State::Building;
  std::string m_forceQuery;
  std::string m_forceState;
  NeuroSDK::ActionPriority m_forcePriority = NeuroSDK::ActionPriority::High;
  bool m_forceEphemeralContext = true;
  std::vector<std::unique_ptr<IAction>> m_actions;
  bool m_ownsForce = false;
};

} // namespace Actions
