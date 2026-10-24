#include "QueryNearbyAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {.name = "query_nearby",
                                .description =
                                    "List up to 20 of the most important nearby actors, containers, doors, loot and "
                                    "other interactable objects "
                                    "(quest targets first) with temporary ids.",
                                .schema = {}};

} // namespace

const Definition &QueryNearbyAction::GetDefinition() const { return kDefinition; }

PreparedAction QueryNearbyAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !GameplayHandler::ValidateGameplayAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { GameplayHandler::QueryNearby(); },
                                 Revalidator([](std::string &revalidationError) {
                                   return GameplayHandler::ValidateGameplayAction(revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
