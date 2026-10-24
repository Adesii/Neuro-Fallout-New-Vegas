#include "DoCurrentQuestAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {.name = "do_current_quest",
                                .description =
                                    "Follow the currently selected quest objective, navigating to and interacting with "
                                    "its target when needed.",
                                .schema = {}};

} // namespace

const Definition &DoCurrentQuestAction::GetDefinition() const { return kDefinition; }

PreparedAction DoCurrentQuestAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  GameplayHandler::QuestSelection selection;
  if (!GameplayHandler::PrepareCurrentQuest(selection, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([selection]() { GameplayHandler::DoCurrentQuest(selection); },
                                 Revalidator([selection](std::string &revalidationError) {
                                   return GameplayHandler::RevalidateCurrentQuest(selection, revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
