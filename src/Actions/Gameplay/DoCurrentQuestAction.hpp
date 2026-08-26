#pragma once

#include "Actions/Action.hpp"

namespace Actions::Gameplay {

class DoCurrentQuestAction final : public IAction {
public:
  DoCurrentQuestAction();
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  Definition m_definition;
};

} // namespace Actions::Gameplay
