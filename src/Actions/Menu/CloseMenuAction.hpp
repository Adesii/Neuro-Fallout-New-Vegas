#pragma once

#include "Actions/Action.hpp"
#include <string>

namespace Actions::Menu {

// One close_* action per not-yet-automated menu (see Menus::StubMenuHandler). It only closes that menu.
class CloseMenuAction final : public IAction {
public:
  CloseMenuAction(unsigned menuId, std::string name, std::string menuTitle);
  const Definition &GetDefinition() const override;
  PreparedAction Validate(const Request &request) override;

private:
  unsigned m_menuId;
  Definition m_definition;
};

} // namespace Actions::Menu
