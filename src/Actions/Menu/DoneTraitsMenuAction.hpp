#pragma once

#include "Actions/Action.hpp"

namespace Actions::Menu {

class DoneTraitsMenuAction final : public IAction {
public:
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;
};

} // namespace Actions::Menu
