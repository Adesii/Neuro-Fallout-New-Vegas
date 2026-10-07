#include "StowAwayItemAction.hpp"
#include "Actions/ActionData.hpp"
#include "Inventory/ItemQuery.hpp"
#include "Menus/ContainerHandler.hpp"
#include <utility>

namespace Actions::Container {
namespace {

const Definition kDefinition = {
    .name = "stow_away_item",
    .description = "Stow the entire listed stack using its one-based stable index from the last own-items listing. "
                   "Query your items first to obtain current indexes.",
    .schema = Inventory::BuildItemIndexSchema()};

} // namespace

const Definition &StowAwayItemAction::GetDefinition() const { return kDefinition; }

PreparedAction StowAwayItemAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int index = 0;
  if (!Inventory::ParseItemIndex(*data, index, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::ContainerHandler::PrepareTransfer(true, index);
}

} // namespace Actions::Container
