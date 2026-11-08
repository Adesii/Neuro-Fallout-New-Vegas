#include "QueryNearbyAction.hpp"
#include "Context/NearbyListing.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {
    .name = "query_nearby",
    .description = "List nearby actors, containers, doors, loot and other interactable objects with temporary ids, 20 "
                   "per page, most important (quest targets) first. Optional page (1 is the first).",
    .schema = Nearby::BuildListSchema()};

} // namespace

const Definition &QueryNearbyAction::GetDefinition() const { return kDefinition; }

PreparedAction QueryNearbyAction::Validate(const Request &request) {
  std::string error;
  auto query = Nearby::ParseListQuery(request.data, error);
  if (!query || !GameplayHandler::ValidateGameplayAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([page = query->page]() { GameplayHandler::QueryNearby(page, {}); },
                                 Revalidator([](std::string &revalidationError) {
                                   return GameplayHandler::ValidateGameplayAction(revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
