#include "DropItemAction.hpp"
#include "Actions/ActionData.hpp"
#include "Inventory/ItemQuery.hpp"
#include "Menus/InventoryHandler.hpp"
#include <utility>

namespace Actions::Inventory {
namespace {

const Definition kDefinition = {.name = "drop_item",
                                .description =
                                    "Drop the entire listed stack at its stable one-based inventory index through the "
                                    "native Pip-Boy controls. Quest items and keyring keys cannot be dropped.",
                                .schema = ::Inventory::BuildItemIndexSchema()};

} // namespace

const Definition &DropItemAction::GetDefinition() const { return kDefinition; }

PreparedAction DropItemAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int index = 0;
  if (!::Inventory::ParseItemIndex(*data, index, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::InventoryHandler::PrepareItem(Menus::InventoryHandler::Operation::Drop, index);
}

} // namespace Actions::Inventory
