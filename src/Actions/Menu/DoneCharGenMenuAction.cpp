#include "DoneCharGenMenuAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/CharGenHandler.hpp"
#include <string>
#include <utility>

namespace Actions::Menu {
namespace {

const Definition kDefinition = {
    .name = "done_char_gen_menu",
    .description = "Finish the tag-skill menu without making another skill change.",
    .schema = {},
};

} // namespace

const Definition &DoneCharGenMenuAction::GetDefinition() const { return kDefinition; }

PreparedAction DoneCharGenMenuAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error))
    return PreparedAction::Failure(std::move(error));
  Menus::CharGenHandler::DoneSnapshot snapshot;
  if (!Menus::CharGenHandler::ValidateDone(snapshot, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([snapshot]() { Menus::CharGenHandler::StartDone(snapshot); },
                                 [snapshot]() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!Menus::CharGenHandler::RevalidateDone(snapshot, revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Menu
