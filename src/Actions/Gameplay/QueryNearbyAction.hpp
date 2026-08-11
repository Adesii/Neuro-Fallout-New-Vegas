#pragma once

#include "Actions/Action.hpp"

namespace Actions::Gameplay {

class QueryNearbyAction final : public IAction {
public:
  QueryNearbyAction();
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  Definition m_definition;
};

} // namespace Actions::Gameplay
