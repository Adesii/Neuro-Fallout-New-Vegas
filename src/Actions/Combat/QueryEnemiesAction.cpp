#include "QueryEnemiesAction.hpp"
#include "Actions/ActionData.hpp"
#include "CombatHandler.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Combat {
namespace {

const Definition kDefinition = {
    .name = "query_enemies",
    .description = "List nearby living actors you could fight, hostile ones first, with temporary ids, health, "
                   "distance, line of sight and your own combat status. Explicitly friendly actors are never listed; "
                   "neutral ones are, because attacking them is your choice.",
    .schema = {}};

} // namespace

const Definition &QueryEnemiesAction::GetDefinition() const { return kDefinition; }

PreparedAction QueryEnemiesAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !GameplayHandler::ValidateGameplayAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { CombatHandler::QueryEnemies(); },
                                 Revalidator([](std::string &revalidationError) {
                                   return GameplayHandler::ValidateGameplayAction(revalidationError);
                                 }));
}

} // namespace Actions::Combat
