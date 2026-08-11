#include "QueryNearbyAction.hpp"
#include "GameplayHandler.hpp"

namespace Actions::Gameplay {

QueryNearbyAction::QueryNearbyAction()
    : m_definition({.name = "query_nearby",
                    .description = "List nearby actors, interactable objects, containers, and loot with temporary ids.",
                    .schema = {}}) {}

const Definition &QueryNearbyAction::GetDefinition() const { return m_definition; }

PreparedAction QueryNearbyAction::Validate(const Request &) {
  return PreparedAction::Success("Nearby-object scan requested.", []() { GameplayHandler::QueryNearby(); });
}

} // namespace Actions::Gameplay
