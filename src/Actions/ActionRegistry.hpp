#pragma once

#include "Action.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Actions {

class ActionWindow;

class ActionRegistry {
public:
  static ActionRegistry &Get();

  bool Bind(IAction &action);
  void Unbind(IAction &action);
  bool Bind(ActionWindow &window);
  void Unbind(ActionWindow &window);
  void Dispatch(std::vector<Request> requests);
  void Reset();
  // Results owed for request ids of a dead connection can never be delivered usefully.
  void DropPendingResults();
  bool HasPendingResult(const std::string &actionName) const;
  bool HasPendingResults() const { return !m_pendingResults.empty(); }
  // Incremented by every Reset(), so owners with their own retry state can tell that the world was reset.
  uint64_t Generation() const { return m_generation; }

private:
  struct Entry {
    IAction *action = nullptr;
    ActionWindow *window = nullptr;
  };

  struct PendingResult {
    std::string id;
    std::string actionName;
    bool success = false;
    std::string message;
    std::function<void()> execute;
    std::function<std::optional<std::string>()> revalidate;
  };

  std::unordered_map<std::string, Entry> m_entries;
  std::vector<PendingResult> m_pendingResults;
  uint64_t m_generation = 0;

  void DeliverOrQueue(PendingResult result);
  void RetryPendingResults();
};

} // namespace Actions
