#include "SearchNearbyAction.hpp"
#include "Context/NearbyListing.hpp"
#include "GameplayHandler.hpp"
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {
    .name = "search_nearby",
    .description = "Search nearby actors and objects by name or kind (such as door or container), best match first, "
                   "20 per page, with the same temporary ids as query_nearby. Partial words and small typos are fine. "
                   "Optional page (1 is the first).",
    .schema = Nearby::BuildSearchSchema()};

} // namespace

const Definition &SearchNearbyAction::GetDefinition() const { return kDefinition; }

PreparedAction SearchNearbyAction::Validate(const Request &request) {
  std::string error;
  auto query = Nearby::ParseSearchQuery(request.data, error);
  if (!query || !GameplayHandler::ValidateGameplayAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success(
      [page = query->page, search = std::move(query->search)]() { GameplayHandler::QueryNearby(page, search); },
      Revalidator(
          [](std::string &revalidationError) { return GameplayHandler::ValidateGameplayAction(revalidationError); }));
}

} // namespace Actions::Gameplay
