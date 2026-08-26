#include "PersistentActionSet.hpp"
#include "ActionRegistry.hpp"
#include "NeuroSDK.hpp"
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

PersistentActionSet &PersistentActionSet::Add(std::unique_ptr<IAction> action) {
  if (!m_registered && action)
    m_actions.push_back(std::move(action));
  return *this;
}

bool PersistentActionSet::Register() {
  if (m_registered)
    return true;
  if (m_actions.empty())
    return false;

  std::vector<Definition> definitions;
  std::unordered_set<std::string> names;
  definitions.reserve(m_actions.size());
  for (const auto &action : m_actions) {
    const auto &definition = action->GetDefinition();
    if (!IsValidActionName(definition.name) || !names.insert(definition.name).second) {
      _WARNING("PersistentActionSet rejected invalid or duplicate action name: %s", definition.name.c_str());
      return false;
    }
    definitions.push_back(definition);
  }

  size_t boundCount = 0;
  for (const auto &action : m_actions) {
    if (!ActionRegistry::Get().Bind(*action)) {
      _WARNING("PersistentActionSet failed to bind action: %s", action->GetDefinition().name.c_str());
      for (size_t index = 0; index < boundCount; ++index)
        ActionRegistry::Get().Unbind(*m_actions[index]);
      return false;
    }
    ++boundCount;
  }

  if (!NeuroSDK::RegisterActions(definitions)) {
    for (const auto &action : m_actions)
      ActionRegistry::Get().Unbind(*action);
    return false;
  }

  m_registered = true;
  _MESSAGE("PersistentActionSet registered %zu action(s)", m_actions.size());
  return true;
}

void PersistentActionSet::Abandon() {
  for (const auto &action : m_actions)
    ActionRegistry::Get().Unbind(*action);
  m_registered = false;
}

bool PersistentActionSet::IsRegistered() const { return m_registered; }

} // namespace Actions
