#include "DoCurrentQuestAction.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {

DoCurrentQuestAction::DoCurrentQuestAction()
    : m_definition({.name = "do_current_quest",
                    .description = "Follow the currently selected quest objective, navigating to and interacting with "
                                   "its target when needed.",
                    .schema = {}}) {}

const Definition &DoCurrentQuestAction::GetDefinition() const { return m_definition; }

PreparedAction DoCurrentQuestAction::Validate(const Request &) {
  GameplayHandler::QuestSelection selection;
  std::string error;
  if (!GameplayHandler::PrepareCurrentQuest(selection, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success(
      "Following " + selection.description + ".", [selection]() { GameplayHandler::DoCurrentQuest(selection); },
      [selection]() -> std::optional<std::string> {
        std::string revalidationError;
        if (!GameplayHandler::RevalidateCurrentQuest(selection, revalidationError))
          return revalidationError;
        return std::nullopt;
      });
}

} // namespace Actions::Gameplay
