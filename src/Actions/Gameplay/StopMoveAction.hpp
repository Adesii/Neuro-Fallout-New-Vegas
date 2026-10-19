#pragma once

#include "Actions/Action.hpp"

namespace Actions::Gameplay {

class StopMoveAction final : public IAction {
public:
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;
};

} // namespace Actions::Gameplay
