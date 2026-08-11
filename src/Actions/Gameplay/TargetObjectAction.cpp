#include "TargetObjectAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "GameplayHandler.hpp"
#include "WalkerHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

constexpr const char *kId = "id";
constexpr const char *kGeneration = "generation";

Definition BuildDefinition(bool interact) {
  auto schema = Json::JsonSchema::Object();
  auto id = Json::JsonSchema::Integer();
  id.Minimum(1);
  auto generation = Json::JsonSchema::Integer();
  generation.Minimum(1);
  schema.Property(kGeneration, std::move(generation), true);
  schema.Property(kId, std::move(id), true);
  return {.name = interact ? "interact_with_object" : "move_to_object",
          .description = interact ? "Move to and interact using a generation and id returned by query_nearby."
                                  : "Move without interacting using a generation and id returned by query_nearby.",
          .schema = std::move(schema)};
}

} // namespace

TargetObjectAction::TargetObjectAction(bool interact) : m_interact(interact), m_definition(BuildDefinition(interact)) {}

const Definition &TargetObjectAction::GetDefinition() const { return m_definition; }

PreparedAction TargetObjectAction::Validate(const Request &request) {
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

  GameplayHandler::ObjectSelection selection;
  if (!GameplayHandler::ValidateObjectSelection(generation, id, selection, error))
    return PreparedAction::Failure(std::move(error));
  const Walker::Intent intent = m_interact ? Walker::Intent::Interact : Walker::Intent::Move;
  return PreparedAction::Success(
      std::string(m_interact ? "Moving to interact with " : "Moving to ") + selection.name + ".",
      [selection, intent]() { GameplayHandler::StartObjectAction(selection, intent); },
      [selection]() -> std::optional<std::string> {
        std::string revalidationError;
        if (!GameplayHandler::RevalidateObjectSelection(selection, revalidationError))
          return revalidationError;
        return std::nullopt;
      });
}

} // namespace Actions::Gameplay
