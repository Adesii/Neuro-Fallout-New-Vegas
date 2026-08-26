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

} // namespace

SelectQuestAction::SelectQuestAction() : m_definition(BuildDefinition()) {}

const Definition &SelectQuestAction::GetDefinition() const { return m_definition; }

PreparedAction SelectQuestAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  if (!data->IsObject() || data->Size() != 1)
    return PreparedAction::Failure("Expected exactly one integer property named id.");
  int id = 0;
  if (!data->GetInteger(kId, id))
    return PreparedAction::Failure("id must be an integer.");

  GameplayHandler::QuestSelection selection;
  if (!GameplayHandler::ValidateQuestSelection(id, selection, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success(
      "Selected " + selection.description + ".", [selection]() { GameplayHandler::SelectQuest(selection); },
      [selection]() -> std::optional<std::string> {
        std::string revalidationError;
        if (!GameplayHandler::RevalidateQuestSelection(selection, revalidationError))
          return revalidationError;
        return std::nullopt;
      });
}

} // namespace Actions::Gameplay
