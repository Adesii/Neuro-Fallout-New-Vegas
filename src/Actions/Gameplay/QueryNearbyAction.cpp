#include "QueryNearbyAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {

QueryNearbyAction::QueryNearbyAction()
    : m_definition(
          {.name = "query_nearby",
           .description =
               "List nearby actors, activators, containers, loot, and other interactable objects with temporary ids.",
           .schema = {}}) {}

const Definition &QueryNearbyAction::GetDefinition() const { return m_definition; }

PreparedAction QueryNearbyAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !GameplayHandler::ValidateGameplayAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { GameplayHandler::QueryNearby(); },
                                 []() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!GameplayHandler::ValidateGameplayAction(revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Gameplay
