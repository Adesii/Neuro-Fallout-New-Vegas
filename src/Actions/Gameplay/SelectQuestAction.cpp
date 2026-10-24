#include "SelectQuestAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

constexpr const char *kId = "id";

Definition BuildDefinition() {
  auto schema = Json::JsonSchema::Object();
  auto id = Json::JsonSchema::Integer();
  id.Minimum(1);
  schema.Property(kId, std::move(id), true);
  return {.name = "select_quest",
          .description = "Select a quest and report its current objective using an id returned by query_quests. This "
                         "does not start travel.",
          .schema = std::move(schema)};
}

const Definition kDefinition = BuildDefinition();

} // namespace

const Definition &SelectQuestAction::GetDefinition() const { return kDefinition; }

PreparedAction SelectQuestAction::Validate(const Request &request) {
  std::string error;
  int id = 0;
  if (!ActionData::ParsePositiveId(request.data, kId, id, error))
    return PreparedAction::Failure(std::move(error));

  GameplayHandler::QuestSelection selection;
  if (!GameplayHandler::ValidateQuestSelection(id, selection, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([selection]() { GameplayHandler::SelectQuest(selection); },
                                 Revalidator([selection](std::string &revalidationError) {
                                   return GameplayHandler::RevalidateQuestSelection(selection, revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
