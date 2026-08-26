#include "DoneTraitsMenuAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/TraitsHandler.hpp"
#include <string>
#include <utility>

namespace Actions::Menu {
namespace {

const Definition kDefinition = {
    .name = "done_traits_menu",
    .description = "Finish the traits menu without selecting another trait. Choosing no traits is valid.",
    .schema = {},
};

} // namespace

const Definition &DoneTraitsMenuAction::GetDefinition() const { return kDefinition; }

PreparedAction DoneTraitsMenuAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  Menus::TraitsHandler::DoneSnapshot snapshot;
  if (!Menus::TraitsHandler::ValidateDone(snapshot, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([snapshot]() { Menus::TraitsHandler::StartDone(snapshot); },
                                 [snapshot]() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!Menus::TraitsHandler::RevalidateDone(snapshot, revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Menu
