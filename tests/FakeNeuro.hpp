#pragma once

#include "Actions/Action.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Fake {

// Ordered record of everything the action layer sent, plus anything tests append with Log().
struct Neuro {
  std::vector<std::string> log;
  bool failRegister = false;
  bool failUnregister = false;
  bool failForce = false;
  bool failResult = false;

  void Reset() { *this = Neuro{}; }
  size_t Count(const std::string &entry) const;
  bool Contains(const std::string &prefix) const;
};

Neuro &State();
void Log(const std::string &entry);

// Configurable concrete action: Validate delegates to a lambda.
class Action final : public Actions::IAction {
public:
  using Handler = std::function<Actions::PreparedAction(const Actions::Request &)>;

  explicit Action(std::string name, Handler handler = {});
  const Actions::Definition &GetDefinition() const override { return m_definition; }
  Actions::PreparedAction Validate(const Actions::Request &request) override;

private:
  Actions::Definition m_definition;
  Handler m_handler;
};

// Clears the fake transport, the process-wide registry and any pending results.
void ResetWorld();

} // namespace Fake
