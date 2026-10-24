#include "QueryQuestsAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {
    .name = "query_quests",
    .description = "List current incomplete quest objectives and ids that can be passed to select_quest.",
    .schema = {}};

} // namespace

const Definition &QueryQuestsAction::GetDefinition() const { return kDefinition; }

PreparedAction QueryQuestsAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !GameplayHandler::ValidateQuestAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { GameplayHandler::QueryQuests(); },
                                 Revalidator([](std::string &revalidationError) {
                                   return GameplayHandler::ValidateQuestAction(revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
