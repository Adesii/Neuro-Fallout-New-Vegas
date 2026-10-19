#include "UnequipItemAction.hpp"
#include "Actions/ActionData.hpp"
#include "Inventory/ItemQuery.hpp"
#include "Menus/InventoryHandler.hpp"
#include <utility>

namespace Actions::Inventory {
namespace {

const Definition kDefinition = {
    .name = "unequip_item",
    .description = "Unequip the weapon, armor or clothing item at a stable one-based inventory index. Already "
                   "unequipped items stay unequipped; query_own_items refreshes indexes and equipped states.",
    .schema = ::Inventory::BuildItemIndexSchema()};

} // namespace

const Definition &UnequipItemAction::GetDefinition() const { return kDefinition; }

PreparedAction UnequipItemAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int index = 0;
  if (!::Inventory::ParseItemIndex(*data, index, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::InventoryHandler::PrepareItem(Menus::InventoryHandler::Operation::Unequip, index);
}

} // namespace Actions::Inventory
