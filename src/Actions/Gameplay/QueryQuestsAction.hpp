#pragma once

#include "Actions/Action.hpp"

namespace Actions::Gameplay {

class QueryQuestsAction final : public IAction {
public:
  QueryQuestsAction();
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  Definition m_definition;
};

} // namespace Actions::Gameplay
