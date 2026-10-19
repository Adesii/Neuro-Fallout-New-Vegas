#include "LootItemAction.hpp"
#include "Actions/ActionData.hpp"
#include "Inventory/ItemQuery.hpp"
#include "Menus/ContainerHandler.hpp"
#include <utility>

namespace Actions::Container {
namespace {

const Definition kDefinition = {
    .name = "loot_item",
    .description = "Take the entire listed stack using its one-based stable index from the last container listing. "
                   "The initial open-container listing supports indexes 1 through 15.",
    .schema = Inventory::BuildItemIndexSchema()};

} // namespace

const Definition &LootItemAction::GetDefinition() const { return kDefinition; }

PreparedAction LootItemAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int index = 0;
  if (!Inventory::ParseItemIndex(*data, index, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::ContainerHandler::PrepareTransfer(false, index);
}

} // namespace Actions::Container
