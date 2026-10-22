#include "ActionRegistry.hpp"
#include "ActionWindow.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include <algorithm>
#include <utility>

namespace Actions {

ActionRegistry &ActionRegistry::Get() {
  // Leaked on purpose: action windows owned by namespace-scope statics unbind from it in their destructors, which may
  // run after a function-local static would already have been destroyed.
  static auto *registry = new ActionRegistry;
  return *registry;
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
    auto reject = [&](std::string message) {
      DeliverOrQueue({.id = request.id,
                      .actionName = request.name,
                      .success = false,
                      .message = std::move(message),
                      .execute = {},
                      .revalidate = {}});
    };

    if (request.id.empty()) {
      // Nothing can be answered or acknowledged without an id; leave the window alone so Neuro can retry.
      _WARNING("ActionRegistry dropped action '%s' because its request id is empty", request.name.c_str());
      continue;
    }

    auto entry = m_entries.find(request.name);
    if (entry == m_entries.end()) {
      _WARNING("ActionRegistry rejected unavailable action: %s", request.name.c_str());
      reject("Unknown or unavailable action `" + request.name + "`. Choose from the currently registered actions.");
      continue;
    }
    if (HasPendingResult(request.name)) {
      reject("A previous request for `" + request.name + "` is still awaiting its result. Wait before retrying.");
      continue;
    }

    PreparedAction prepared = entry->second.action->Validate(request);
    if (!prepared.valid) {
      _WARNING("Action '%s' failed validation: %s", request.name.c_str(), prepared.message.c_str());
      reject(std::move(prepared.message));
      continue;
    }
    if (prepared.revalidate) {
      auto error = prepared.revalidate();
      if (error) {
        _WARNING("Action '%s' failed revalidation: %s", request.name.c_str(), error->c_str());
        reject(std::move(*error));
        continue;
      }
    }

    if (entry->second.window && !entry->second.window->End()) {
      reject("The decision changed while the action was being accepted. Choose again.");
      continue;
    }
    DeliverOrQueue({.id = request.id,
                    .actionName = request.name,
                    .success = true,
                    .message = {},
                    .execute = std::move(prepared.execute),
                    .revalidate = std::move(prepared.revalidate)});
  }
}

void ActionRegistry::Reset() {
  ++m_generation;
  m_entries.clear();
  for (auto &pending : m_pendingResults) {
    if (pending.success) {
      pending.success = false;
      pending.message = "Game state changed before the action could execute.";
    }
    pending.execute = {};
    pending.revalidate = {};
  }
}

void ActionRegistry::DropPendingResults() { m_pendingResults.clear(); }

bool ActionRegistry::HasPendingResult(const std::string &actionName) const {
  return std::any_of(m_pendingResults.begin(), m_pendingResults.end(),
                     [&](const PendingResult &pending) { return pending.actionName == actionName; });
}

void ActionRegistry::DeliverOrQueue(PendingResult result) {
  if (!NeuroSDK::SendActionResult(result.id, result.success, result.message)) {
    _WARNING("Action '%s' result send deferred", result.actionName.c_str());
    m_pendingResults.push_back(std::move(result));
    return;
  }

  if (result.success && result.execute)
    result.execute();
  _MESSAGE("Action '%s' result delivered", result.actionName.c_str());
}

void ActionRegistry::RetryPendingResults() {
  // Executions run after the loop: a callback may push new pending results or reset the registry, which would
  // invalidate the iterators of the loop below.
  struct Finished {
    std::string actionName;
    std::function<void()> execute;
  };
  std::vector<Finished> finished;

  for (auto iter = m_pendingResults.begin(); iter != m_pendingResults.end();) {
    if (iter->success && iter->revalidate) {
      auto error = iter->revalidate();
      if (error) {
        iter->success = false;
        iter->message = std::move(*error);
        iter->execute = {};
        iter->revalidate = {};
      }
    }
    if (!NeuroSDK::SendActionResult(iter->id, iter->success, iter->message)) {
      ++iter;
      continue;
    }

    finished.push_back({iter->actionName, iter->success ? std::move(iter->execute) : std::function<void()>{}});
    iter = m_pendingResults.erase(iter);
  }

  for (auto &entry : finished) {
    if (entry.execute)
      entry.execute();
    _MESSAGE("Deferred action '%s' result delivered", entry.actionName.c_str());
  }
}

} // namespace Actions
