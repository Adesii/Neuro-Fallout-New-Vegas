#pragma once

#include "Actions/Action.hpp"
#include <cstddef>

namespace Actions::Menu {

class SelectDialogAction final : public IAction {
public:
  explicit SelectDialogAction(size_t optionCount);

  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  Definition m_definition;
};

} // namespace Actions::Menu
