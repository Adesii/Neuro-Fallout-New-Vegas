#pragma once

#include "Action.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace Actions {

class ActionWindow;

class ActionRegistry {
public:
  static ActionRegistry &Get();

  bool Bind(ActionWindow &window);
  void Unbind(ActionWindow &window);
  void Dispatch(std::vector<Request> requests);
  void Reset();
  bool HasPendingResult(const std::string &actionName) const;

private:
  struct Entry {
    IAction *action = nullptr;
    ActionWindow *window = nullptr;
  };

  struct PendingResult {
    std::string id;
    std::string actionName;
    std::string message;
    std::function<void()> execute;
    std::function<std::optional<std::string>()> revalidate;
  };

  std::unordered_map<std::string, Entry> m_entries;
  std::vector<PendingResult> m_pendingResults;

  void RetryPendingResults();
};

} // namespace Actions
