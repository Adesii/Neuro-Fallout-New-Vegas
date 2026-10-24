#include "ExploreAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {
    .name = "explore",
    .description = "Explore toward a varied, interesting, not-recently-visited object in the loaded area.",
    .schema = {}};

} // namespace

const Definition &ExploreAction::GetDefinition() const { return kDefinition; }

PreparedAction ExploreAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  GameplayHandler::ObjectSelection selection;
  if (!GameplayHandler::PrepareExploration(selection, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([selection]() { GameplayHandler::StartExploration(selection); },
                                 Revalidator([selection](std::string &revalidationError) {
                                   return GameplayHandler::RevalidateObjectSelection(selection, revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
