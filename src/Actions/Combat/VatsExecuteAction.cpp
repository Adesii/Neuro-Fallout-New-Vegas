#include "VatsExecuteAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/VatsHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Combat {
namespace {

const Definition kDefinition = {.name = "vats_execute",
                                .description =
                                    "Confirm and run all queued V.A.T.S. shots. Needs at least one queued shot.",
                                .schema = {}};

} // namespace

const Definition &VatsExecuteAction::GetDefinition() const { return kDefinition; }

PreparedAction VatsExecuteAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !Menus::VatsHandler::ValidateExecute(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { Menus::VatsHandler::StartExecute(); },
                                 []() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!Menus::VatsHandler::ValidateExecute(revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Combat
