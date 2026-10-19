#include "OpenVatsAction.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Combat/CombatLogic.hpp"
#include "CombatHandler.hpp"
#include "Menus/VatsHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Combat {
namespace {

Definition BuildDefinition() {
  auto schema = Json::JsonSchema::Object();
  auto enemy = Json::JsonSchema::Integer();
  enemy.Minimum(1);
  schema.Property("enemy_id", std::move(enemy), false);
  return {.name = "open_vats",
          .description = "Open V.A.T.S. to queue targeted shots with your action points. Optionally pass enemy_id "
                         "(from query_enemies) to face that enemy first so it is selected. Once open, you choose "
                         "targets and body parts there.",
          .schema = std::move(schema)};
}

const Definition kDefinition = BuildDefinition();

} // namespace

const Definition &OpenVatsAction::GetDefinition() const { return kDefinition; }

PreparedAction OpenVatsAction::Validate(const Request &request) {
  std::string error;
  std::optional<int> enemyId;
  if (!::Combat::ParseOptionalEnemyId(request.data, enemyId, error))
    return PreparedAction::Failure(std::move(error));

  std::optional<CombatHandler::TargetSelection> target;
  if (enemyId) {
    CombatHandler::TargetSelection selection;
    if (!CombatHandler::ResolveEnemy(*enemyId, selection, error))
      return PreparedAction::Failure(std::move(error));
    target = std::move(selection);
  }
  return Menus::VatsHandler::PrepareOpen(std::move(target));
}

} // namespace Actions::Combat
