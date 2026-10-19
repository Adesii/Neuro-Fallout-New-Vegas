#pragma once

#include "Actions/Action.hpp"
#include <cstddef>

namespace Actions::Menu {

class SelectPopupOptionAction final : public IAction {
public:
  explicit SelectPopupOptionAction(size_t optionCount);

  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  Definition m_definition;
};

} // namespace Actions::Menu
