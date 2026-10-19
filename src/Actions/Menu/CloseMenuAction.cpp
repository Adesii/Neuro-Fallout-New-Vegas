#include "CloseMenuAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/StubMenuHandler.hpp"
#include <utility>

namespace Actions::Menu {

CloseMenuAction::CloseMenuAction(unsigned menuId, std::string name, std::string menuTitle)
    : m_menuId(menuId),
      m_definition({.name = std::move(name),
                    .description = "Close the " + menuTitle + " menu. Its other controls are not available to you yet.",
                    .schema = {}}) {}

const Definition &CloseMenuAction::GetDefinition() const { return m_definition; }

PreparedAction CloseMenuAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::StubMenuHandler::PrepareClose(m_menuId);
}

} // namespace Actions::Menu
