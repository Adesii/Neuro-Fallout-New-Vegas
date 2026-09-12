#include "QueryQuestsAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {

QueryQuestsAction::QueryQuestsAction()
    : m_definition(
          {.name = "query_quests",
           .description = "List current incomplete quest objectives and ids that can be passed to select_quest.",
           .schema = {}}) {}

const Definition &QueryQuestsAction::GetDefinition() const { return m_definition; }

PreparedAction QueryQuestsAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !GameplayHandler::ValidateQuestAction(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { GameplayHandler::QueryQuests(); },
                                 []() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!GameplayHandler::ValidateQuestAction(revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Gameplay
