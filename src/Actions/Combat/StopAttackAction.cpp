#include "StopAttackAction.hpp"
#include "Actions/ActionData.hpp"
#include "CombatHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Combat {
namespace {

const Definition kDefinition = {.name = "stop_attack",
                                .description =
                                    "Stop attacking your current target, for example to run away or take cover.",
                                .schema = {}};

} // namespace

const Definition &StopAttackAction::GetDefinition() const { return kDefinition; }

PreparedAction StopAttackAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !CombatHandler::ValidateStopAttack(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { CombatHandler::StopAttack(); }, Revalidator([](std::string &revalidationError) {
                                   return CombatHandler::ValidateStopAttack(revalidationError);
                                 }));
}

} // namespace Actions::Combat
