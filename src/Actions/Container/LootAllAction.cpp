#include "LootAllAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/ContainerHandler.hpp"
#include <utility>

namespace Actions::Container {
namespace {

const Definition kDefinition = {
    .name = "loot_all", .description = "Take all items from the currently open container.", .schema = {}};

} // namespace

const Definition &LootAllAction::GetDefinition() const { return kDefinition; }

PreparedAction LootAllAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::ContainerHandler::PrepareLootAll();
}

} // namespace Actions::Container
