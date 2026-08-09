#include "ActionWindow.hpp"
#include "ActionRegistry.hpp"
#include "Utils/DebugLog.hpp"
#include <unordered_set>
#include <utility>

namespace Actions {
namespace {

bool IsValidActionName(const std::string &name) {
  if (name.empty() || name.front() == '_' || name.back() == '_')
    return false;
  for (char character : name) {
    if ((character < 'a' || character > 'z') && character != '_')
      return false;
  }
  return true;
}

} // namespace

ActionWindow &ActionWindow::SetContext(std::string context, bool silent) {
  if (m_state == State::Building) {
    m_context = std::move(context);
    m_contextSilent = silent;
  }
  return *this;
}

ActionWindow &ActionWindow::Add(std::unique_ptr<IAction> action) {
  if (m_state == State::Building && action)
    m_actions.push_back(std::move(action));
  return *this;
}

ActionWindow &ActionWindow::SetForce(std::string query, std::string state, NeuroSDK::ActionPriority priority) {
  if (m_state == State::Building) {
    m_forceQuery = std::move(query);
    m_forceState = std::move(state);
    m_forcePriority = priority;
  }
  return *this;
}

bool ActionWindow::Register() {
  if (m_state != State::Building || m_actions.empty()) {
    _WARNING("ActionWindow registration rejected in state %d with %zu actions", static_cast<int>(m_state),
             m_actions.size());
    return false;
  }

  std::vector<Definition> definitions;
  std::vector<std::string> names;
  std::unordered_set<std::string> uniqueNames;
  definitions.reserve(m_actions.size());
  names.reserve(m_actions.size());
  for (const auto &action : m_actions) {
    const auto &definition = action->GetDefinition();
    if (!IsValidActionName(definition.name) || !uniqueNames.insert(definition.name).second) {
      _WARNING("ActionWindow rejected invalid or duplicate action name: %s", definition.name.c_str());
      m_state = State::Faulted;
      return false;
    }
    definitions.push_back(definition);
    names.push_back(definition.name);
  }

  if (!m_context.empty() && !NeuroSDK::SendContext(m_context.c_str(), m_contextSilent)) {
    _WARNING("ActionWindow failed to send context");
    m_state = State::Faulted;
    return false;
  }
  if (!ActionRegistry::Get().Bind(*this)) {
    _WARNING("ActionWindow failed to bind actions locally");
    m_state = State::Faulted;
    return false;
  }
  if (!NeuroSDK::RegisterActions(definitions)) {
    _WARNING("ActionWindow failed to register actions with NeuroSDK");
    ActionRegistry::Get().Unbind(*this);
    m_state = State::Faulted;
    return false;
  }
  m_state = State::Registered;
  _MESSAGE("ActionWindow registered %zu action(s)", m_actions.size());

  if (!m_forceQuery.empty()) {
    if (!NeuroSDK::ForceActions(names, m_forceQuery, m_forceState, m_forcePriority)) {
      _WARNING("ActionWindow failed to force actions");
      End();
      return false;
    }
    m_state = State::Forced;
    _MESSAGE("ActionWindow forced %zu action(s)", m_actions.size());
  }
  return true;
}

bool ActionWindow::End() {
  if (m_state == State::Ended)
    return true;
  if (m_state == State::Building) {
    m_state = State::Ended;
    return true;
  }

  std::vector<std::string> names;
  names.reserve(m_actions.size());
  for (const auto &action : m_actions)
    names.push_back(action->GetDefinition().name);

  if (m_state != State::Closing) {
    ActionRegistry::Get().Unbind(*this);
    m_state = State::Closing;
  }
  const bool sent = NeuroSDK::UnregisterActions(names);
  if (sent) {
    m_state = State::Ended;
    _MESSAGE("ActionWindow unregistered %zu action(s)", names.size());
  }
  return sent;
}

void ActionWindow::Abandon() {
  ActionRegistry::Get().Unbind(*this);
  m_state = State::Ended;
}

ActionWindow::State ActionWindow::GetState() const { return m_state; }

const std::vector<std::unique_ptr<IAction>> &ActionWindow::GetActions() const { return m_actions; }

} // namespace Actions
