#include "ExploreAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {

ExploreAction::ExploreAction()
    : m_definition(
          {.name = "explore",
           .description = "Explore toward a varied, interesting, not-recently-visited object in the loaded area.",
           .schema = {}}) {}

const Definition &ExploreAction::GetDefinition() const { return m_definition; }

PreparedAction ExploreAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  GameplayHandler::ObjectSelection selection;
  if (!GameplayHandler::PrepareExploration(selection, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([selection]() { GameplayHandler::StartExploration(selection); },
                                 [selection]() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!GameplayHandler::RevalidateObjectSelection(selection, revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Gameplay
