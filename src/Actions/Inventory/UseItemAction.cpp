#include "UseItemAction.hpp"
#include "Actions/ActionData.hpp"
#include "Inventory/ItemQuery.hpp"
#include "Menus/InventoryHandler.hpp"
#include <utility>

namespace Actions::Inventory {
namespace {

const Definition kDefinition = {
    .name = "use_item",
    .description = "Use one aid item from the listed stack at its stable one-based inventory index. Food, drinks, "
                   "chems and ingredients use the native Pip-Boy interaction and confirmation dialogs.",
    .schema = ::Inventory::BuildItemIndexSchema()};

} // namespace

const Definition &UseItemAction::GetDefinition() const { return kDefinition; }

PreparedAction UseItemAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int index = 0;
  if (!::Inventory::ParseItemIndex(*data, index, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::InventoryHandler::PrepareItem(Menus::InventoryHandler::Operation::Use, index);
}

} // namespace Actions::Inventory
