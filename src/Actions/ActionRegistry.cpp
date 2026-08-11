#include "ActionRegistry.hpp"
#include "ActionWindow.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include <algorithm>
#include <utility>

namespace Actions {

ActionRegistry &ActionRegistry::Get() {
  static ActionRegistry registry;
  return registry;
}

bool ActionRegistry::Bind(IAction &action) {
  const auto &name = action.GetDefinition().name;
  return m_entries.emplace(name, Entry{&action, nullptr}).second;
}

void ActionRegistry::Unbind(IAction &action) {
  auto entry = m_entries.find(action.GetDefinition().name);
  if (entry != m_entries.end() && entry->second.action == &action)
    m_entries.erase(entry);
}

bool ActionRegistry::Bind(ActionWindow &window) {
  for (const auto &action : window.GetActions()) {
    const auto &name = action->GetDefinition().name;
    if (m_entries.contains(name))
      return false;
  }
  for (const auto &action : window.GetActions())
    m_entries.emplace(action->GetDefinition().name, Entry{action.get(), &window});
  return true;
}

void ActionRegistry::Unbind(ActionWindow &window) {
  for (auto iter = m_entries.begin(); iter != m_entries.end();) {
    if (iter->second.window == &window)
      iter = m_entries.erase(iter);
    else
      ++iter;
  }
}

void ActionRegistry::Dispatch(std::vector<Request> requests) {
  RetryPendingResults();

  for (const auto &request : requests) {
    _MESSAGE("ActionRegistry received action '%s' (id: %s)", request.name.c_str(), request.id.c_str());
    auto entry = m_entries.find(request.name);
    if (entry == m_entries.end()) {
      _WARNING("ActionRegistry rejected unavailable action: %s", request.name.c_str());
      NeuroSDK::SendActionResult(request.id, false, "Action is no longer available.");
      continue;
    }
    if (HasPendingResult(request.name)) {
      NeuroSDK::SendActionResult(request.id, false, "A previous request for this action is still pending.");
      continue;
    }

    PreparedAction prepared = entry->second.action->Validate(request);
    if (!prepared.valid) {
      _WARNING("Action '%s' failed validation: %s", request.name.c_str(), prepared.message.c_str());
      NeuroSDK::SendActionResult(request.id, false, prepared.message);
      continue;
    }
    if (prepared.revalidate) {
      auto error = prepared.revalidate();
      if (error) {
        _WARNING("Action '%s' failed revalidation: %s", request.name.c_str(), error->c_str());
        NeuroSDK::SendActionResult(request.id, false, *error);
        continue;
      }
    }

    if (entry->second.window && !entry->second.window->End()) {
      NeuroSDK::SendActionResult(request.id, false, "Failed to close the action window.");
      continue;
    }
    if (!NeuroSDK::SendActionResult(request.id, true, prepared.message)) {
      _WARNING("Action '%s' result send deferred", request.name.c_str());
      m_pendingResults.push_back({.id = request.id,
                                  .actionName = request.name,
                                  .message = std::move(prepared.message),
                                  .execute = std::move(prepared.execute),
                                  .revalidate = std::move(prepared.revalidate)});
      continue;
    }

    if (prepared.execute)
      prepared.execute();
    _MESSAGE("Action '%s' accepted and execution started", request.name.c_str());
  }
}

void ActionRegistry::Reset() {
  m_entries.clear();
  m_pendingResults.clear();
}

bool ActionRegistry::HasPendingResult(const std::string &actionName) const {
  return std::any_of(m_pendingResults.begin(), m_pendingResults.end(),
                     [&](const PendingResult &pending) { return pending.actionName == actionName; });
}

void ActionRegistry::CancelPendingResults(const std::vector<std::string> &actionNames, std::string message) {
  for (auto &pending : m_pendingResults) {
    if (std::find(actionNames.begin(), actionNames.end(), pending.actionName) != actionNames.end()) {
      pending.execute = {};
      pending.revalidate = {};
      pending.cancellation = message;
    }
  }
}

void ActionRegistry::RetryPendingResults() {
  for (auto iter = m_pendingResults.begin(); iter != m_pendingResults.end();) {
    if (!iter->cancellation.empty()) {
      if (!NeuroSDK::SendActionResult(iter->id, false, iter->cancellation)) {
        ++iter;
        continue;
      }
      iter = m_pendingResults.erase(iter);
      continue;
    }
    if (iter->revalidate) {
      auto error = iter->revalidate();
      if (error) {
        if (!NeuroSDK::SendActionResult(iter->id, false, *error)) {
          ++iter;
          continue;
        }
        iter = m_pendingResults.erase(iter);
        continue;
      }
    }
    if (!NeuroSDK::SendActionResult(iter->id, true, iter->message)) {
      ++iter;
      continue;
    }
    auto execute = std::move(iter->execute);
    iter = m_pendingResults.erase(iter);
    if (execute)
      execute();
  }
}

} // namespace Actions
