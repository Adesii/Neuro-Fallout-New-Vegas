#include "FakeNeuro.hpp"
#include "Actions/ActionRegistry.hpp"
#include "NeuroSDK.hpp"
#include <algorithm>

namespace {

std::string Join(const std::vector<std::string> &names) {
  std::string joined;
  for (const auto &name : names)
    joined += (joined.empty() ? "" : ",") + name;
  return joined;
}

} // namespace

namespace Fake {

size_t Neuro::Count(const std::string &entry) const { return std::count(log.begin(), log.end(), entry); }

bool Neuro::Contains(const std::string &prefix) const {
  return std::any_of(log.begin(), log.end(), [&](const std::string &entry) { return entry.starts_with(prefix); });
}

Neuro &State() {
  static Neuro state;
  return state;
}

void Log(const std::string &entry) { State().log.push_back(entry); }

Action::Action(std::string name, Handler handler) : m_handler(std::move(handler)) {
  m_definition.name = std::move(name);
  m_definition.description = "test action";
}

Actions::PreparedAction Action::Validate(const Actions::Request &request) {
  if (m_handler)
    return m_handler(request);
  return Actions::PreparedAction::Success([] {});
}

void ResetWorld() {
  State().Reset();
  auto &registry = Actions::ActionRegistry::Get();
  registry.Reset();
  registry.DropPendingResults();
}

} // namespace Fake

bool NeuroSDK::SendContext(const char *message, bool silent) {
  Fake::Log(std::string("context:") + (silent ? "silent:" : "") + message);
  return true;
}

bool NeuroSDK::RegisterActions(const std::vector<Actions::Definition> &definitions) {
  if (Fake::State().failRegister)
    return false;
  std::vector<std::string> names;
  for (const auto &definition : definitions)
    names.push_back(definition.name);
  Fake::Log("register:" + Join(names));
  return true;
}

bool NeuroSDK::UnregisterActions(const std::vector<std::string> &names) {
  if (Fake::State().failUnregister)
    return false;
  Fake::Log("unregister:" + Join(names));
  return true;
}

bool NeuroSDK::ForceActions(const std::vector<std::string> &names, const std::string &, const std::string &,
                            ActionPriority, bool) {
  if (Fake::State().failForce)
    return false;
  Fake::Log("force:" + Join(names));
  return true;
}

bool NeuroSDK::SendActionResult(const std::string &id, bool success, const std::string &message) {
  if (Fake::State().failResult)
    return false;
  Fake::Log("result:" + id + ":" + (success ? "true" : "false") + ":" + message);
  return true;
}
