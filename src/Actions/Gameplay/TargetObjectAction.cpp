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

Definition BuildDefinition(bool interact) {
  auto schema = Json::JsonSchema::Object();
  auto id = Json::JsonSchema::Integer();
  id.Minimum(1);
  schema.Property(kId, std::move(id), true);
  return {.name = interact ? "interact_with_object" : "move_to_object",
          .description = interact ? "Move to and interact using an id returned by query_nearby."
                                  : "Move without interacting using an id returned by query_nearby.",
          .schema = std::move(schema)};
}

} // namespace

TargetObjectAction::TargetObjectAction(bool interact) : m_interact(interact), m_definition(BuildDefinition(interact)) {}

const Definition &TargetObjectAction::GetDefinition() const { return m_definition; }

PreparedAction TargetObjectAction::Validate(const Request &request) {
  std::string error;
  int id = 0;
  if (!ActionData::ParsePositiveId(request.data, kId, id, error))
    return PreparedAction::Failure(std::move(error));

  GameplayHandler::ObjectSelection selection;
  if (!GameplayHandler::ValidateObjectSelection(id, selection, error))
    return PreparedAction::Failure(std::move(error));
  const Walker::Intent intent = m_interact ? Walker::Intent::Interact : Walker::Intent::Move;
  return PreparedAction::Success([selection, intent]() { GameplayHandler::StartObjectAction(selection, intent); },
                                 Revalidator([selection](std::string &revalidationError) {
                                   return GameplayHandler::RevalidateObjectSelection(selection, revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
