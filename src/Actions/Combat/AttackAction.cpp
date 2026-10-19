#include "AttackAction.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Combat/CombatLogic.hpp"
#include "CombatHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Combat {
namespace {

Definition BuildDefinition() {
  auto schema = Json::JsonSchema::Object();
  auto enemy = Json::JsonSchema::Integer();
  enemy.Minimum(1);
  auto object = Json::JsonSchema::Integer();
  object.Minimum(1);
  schema.Property("enemy_id", std::move(enemy), false).Property("object_id", std::move(object), false);
  return {.name = "attack",
          .description =
              "Attack a target with your equipped weapon until it dies, you run out of ammo, or you lose sight of it. "
              "Melee weapons close in by themselves; ranged weapons fire from where you stand. Give exactly one of "
              "enemy_id (from query_enemies) or object_id (from query_nearby, for shooting things like bottles). "
              "Call it again to change target, and keep moving or switching weapons while it runs.",
          .schema = std::move(schema)};
}

const Definition kDefinition = BuildDefinition();

} // namespace

const Definition &AttackAction::GetDefinition() const { return kDefinition; }

PreparedAction AttackAction::Validate(const Request &request) {
  std::string error;
  ::Combat::AttackRequest attack;
  if (!::Combat::ParseAttackRequest(request.data, attack, error))
    return PreparedAction::Failure(std::move(error));

  CombatHandler::TargetSelection selection;
  if (!CombatHandler::ValidateAttack(attack, selection, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([selection]() { CombatHandler::StartAttack(selection); },
                                 [selection]() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!CombatHandler::RevalidateAttack(selection, revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Combat
