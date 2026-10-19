#include "OpenInventoryAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/PipBoyHandler.hpp"
#include <utility>

namespace Actions::PipBoy {
namespace {

const Definition kDefinition = {
    .name = "open_inventory",
    .description = "Visibly open the Pip-Boy Items tab to manage your inventory. The initial listing supplies up to "
                   "15 stable one-based item indexes; query_own_items supplies other pages and filters.",
    .schema = {}};

} // namespace

const Definition &OpenInventoryAction::GetDefinition() const { return kDefinition; }

PreparedAction OpenInventoryAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::PipBoyHandler::PrepareOpen(Menus::PipBoyHandler::Tab::Items);
}

} // namespace Actions::PipBoy
