#include "SelectQuestAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

constexpr const char *kId = "id";
constexpr const char *kGeneration = "generation";

Definition BuildDefinition() {
  auto schema = Json::JsonSchema::Object();
  auto id = Json::JsonSchema::Integer();
  id.Minimum(1);
  auto generation = Json::JsonSchema::Integer();
  generation.Minimum(1);
  schema.Property(kGeneration, std::move(generation), true);
  schema.Property(kId, std::move(id), true);
  return {.name = "select_quest",
          .description = "Select and follow a quest target using the generation and id returned by query_quests.",
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
  if (!data->IsObject() || data->Size() != 2)
    return PreparedAction::Failure("Expected exactly two integer properties named generation and id.");
  int generation = 0;
  int id = 0;
  if (!data->GetInteger(kGeneration, generation) || !data->GetInteger(kId, id))
    return PreparedAction::Failure("generation and id must be integers.");

  GameplayHandler::QuestSelection selection;
  if (!GameplayHandler::ValidateQuestSelection(generation, id, selection, error))
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
