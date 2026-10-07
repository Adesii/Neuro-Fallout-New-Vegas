#pragma once

#include "Actions/Action.hpp"

namespace Actions::Inventory {

class UnequipItemAction final : public IAction {
public:
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;
};

} // namespace Actions::Inventory
