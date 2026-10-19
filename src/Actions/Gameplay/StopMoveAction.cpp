#include "StopMoveAction.hpp"
#include "Actions/ActionData.hpp"
#include "GameplayHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {
    .name = "stop_move",
    .description = "Stop walking toward whatever you are currently moving to, including a quest route or exploring. "
                   "Use stop_attack instead to stop fighting.",
    .schema = {}};

} // namespace

const Definition &StopMoveAction::GetDefinition() const { return kDefinition; }

PreparedAction StopMoveAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  GameplayHandler::MoveStop stop;
  if (!GameplayHandler::PrepareStopMove(stop, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([stop]() { GameplayHandler::StopMove(stop); },
                                 [stop]() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!GameplayHandler::RevalidateStopMove(stop, revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Gameplay
