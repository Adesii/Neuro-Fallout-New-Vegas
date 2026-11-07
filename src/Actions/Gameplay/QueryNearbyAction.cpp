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
                   "per page, most important (quest targets) first. Optional page (1 is the first) and search (a name "
                   "or kind such as door or container; partial words and small typos are fine).",
    .schema = Nearby::BuildQuerySchema()};

} // namespace

const Definition &QueryNearbyAction::GetDefinition() const { return kDefinition; }

PreparedAction QueryNearbyAction::Validate(const Request &request) {
  std::string error;
  auto query = Nearby::ParseQuery(request.data, error);
  if (!query || !GameplayHandler::ValidateGameplayAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success(
      [page = query->page, search = std::move(query->search)]() { GameplayHandler::QueryNearby(page, search); },
      Revalidator(
          [](std::string &revalidationError) { return GameplayHandler::ValidateGameplayAction(revalidationError); }));
}

} // namespace Actions::Gameplay
