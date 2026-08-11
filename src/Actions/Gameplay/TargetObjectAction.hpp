#pragma once

#include "Actions/Action.hpp"

namespace Actions::Gameplay {

class TargetObjectAction final : public IAction {
public:
  explicit TargetObjectAction(bool interact);
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  bool m_interact = false;
  Definition m_definition;
};

} // namespace Actions::Gameplay
