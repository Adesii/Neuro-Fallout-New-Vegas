#include "QueryItemsAction.hpp"
#include "Actions/ActionData.hpp"
#include "Inventory/ItemQuery.hpp"
#include "Menus/ContainerHandler.hpp"
#include <utility>

namespace Actions::Container {
namespace {

const Definition kDefinition = {
    .name = "query_items",
    .description =
        "List container items on a zero-based page of 15 items; index is the page number. The optional "
        "type filter defaults to All. Weapons includes ammunition; WeaponMods and Keys have separate filters.",
    .schema = Inventory::BuildItemQuerySchema()};

} // namespace

const Definition &QueryItemsAction::GetDefinition() const { return kDefinition; }

PreparedAction QueryItemsAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  int page = 0;
  Inventory::ItemQueryType type = Inventory::ItemQueryType::All;
  if (!Inventory::ParseItemQuery(*data, page, type, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::ContainerHandler::PrepareQuery(false, page, type);
}

} // namespace Actions::Container
