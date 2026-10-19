#include "CloseContainerAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/ContainerHandler.hpp"
#include <utility>

namespace Actions::Container {
namespace {

const Definition kDefinition = {
    .name = "close_container",
    .description = "Close the currently open container without taking or storing anything else. Wait for accepted "
                   "item transfers to finish first.",
    .schema = {}};

} // namespace

const Definition &CloseContainerAction::GetDefinition() const { return kDefinition; }

PreparedAction CloseContainerAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::ContainerHandler::PrepareClose();
}

} // namespace Actions::Container
