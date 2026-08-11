#pragma once

#include "Actions/Action.hpp"

namespace Actions::Gameplay {

class ExploreAction final : public IAction {
public:
  ExploreAction();
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  Definition m_definition;
};

} // namespace Actions::Gameplay
