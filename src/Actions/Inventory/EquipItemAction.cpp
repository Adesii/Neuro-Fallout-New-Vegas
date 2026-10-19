#include "EquipItemAction.hpp"
#include "Actions/ActionData.hpp"
#include "Inventory/ItemQuery.hpp"
#include "Menus/InventoryHandler.hpp"
#include <utility>

namespace Actions::Inventory {
namespace {

const Definition kDefinition = {
    .name = "equip_item",
    .description = "Equip one weapon, armor or clothing item using its stable one-based index from the last inventory "
                   "listing. Already equipped items stay equipped; ammunition and weapon mods cannot be equipped.",
    .schema = ::Inventory::BuildItemIndexSchema()};

} // namespace

const Definition &EquipItemAction::GetDefinition() const { return kDefinition; }

PreparedAction EquipItemAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int index = 0;
  if (!::Inventory::ParseItemIndex(*data, index, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::InventoryHandler::PrepareItem(Menus::InventoryHandler::Operation::Equip, index);
}

} // namespace Actions::Inventory
