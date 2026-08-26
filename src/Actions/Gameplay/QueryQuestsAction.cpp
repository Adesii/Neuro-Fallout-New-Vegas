#include "QueryQuestsAction.hpp"
#include "GameplayHandler.hpp"

namespace Actions::Gameplay {

QueryQuestsAction::QueryQuestsAction()
    : m_definition(
          {.name = "query_quests",
           .description = "List current incomplete quest objectives and ids that can be passed to select_quest.",
           .schema = {}}) {}

const Definition &QueryQuestsAction::GetDefinition() const { return m_definition; }

PreparedAction QueryQuestsAction::Validate(const Request &) {
  return PreparedAction::Success("Quest list requested.", []() { GameplayHandler::QueryQuests(); });
}

} // namespace Actions::Gameplay
