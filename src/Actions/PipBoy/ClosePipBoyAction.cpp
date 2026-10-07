#include "ClosePipBoyAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/PipBoyHandler.hpp"
#include <utility>

namespace Actions::PipBoy {
namespace {

const Definition kDefinition = {
    .name = "close_pipboy",
    .description = "Visibly close the Pip-Boy and resume gameplay. Wait for accepted inventory actions and native "
                   "confirmation dialogs to finish first.",
    .schema = {}};

} // namespace

const Definition &ClosePipBoyAction::GetDefinition() const { return kDefinition; }

PreparedAction ClosePipBoyAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  return Menus::PipBoyHandler::PrepareClose();
}

} // namespace Actions::PipBoy
