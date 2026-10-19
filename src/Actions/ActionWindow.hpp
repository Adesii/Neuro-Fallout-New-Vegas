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

  ActionWindow() = default;
  ActionWindow(const ActionWindow &) = delete;
  ActionWindow &operator=(const ActionWindow &) = delete;
  // Never leaves the process-wide force guard held by a window that no longer exists.
  ~ActionWindow();

  ActionWindow &Add(std::unique_ptr<IAction> action);
  ActionWindow &SetForce(std::string query, std::string state,
                         NeuroSDK::ActionPriority priority = NeuroSDK::ActionPriority::High,
                         bool ephemeralContext = true);

  bool Register();
  bool End();
  void Abandon();
  // End() so the server drops the actions and any force; Abandon() when it cannot be notified.
  void Release();
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
