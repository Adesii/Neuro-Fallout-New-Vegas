#include "CrouchToggleAction.hpp"
#include "Actions/ActionData.hpp"
#include "StanceHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Gameplay {
namespace {

const Definition kDefinition = {.name = "crouch_toggle",
                                .description = "Crouch if you are standing, or stand up if you are crouching. "
                                               "Crouching makes you sneak, which makes you harder to detect.",
                                .schema = {}};

} // namespace

const Definition &CrouchToggleAction::GetDefinition() const { return kDefinition; }

PreparedAction CrouchToggleAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !StanceHandler::ValidateCrouchToggle(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { StanceHandler::ToggleCrouch(); },
                                 Revalidator([](std::string &revalidationError) {
                                   return StanceHandler::ValidateCrouchToggle(revalidationError);
                                 }));
}

} // namespace Actions::Gameplay
