#include "VatsCancelAction.hpp"
#include "Actions/ActionData.hpp"
#include "Menus/VatsHandler.hpp"
#include <optional>
#include <utility>

namespace Actions::Combat {
namespace {

const Definition kDefinition = {.name = "vats_cancel",
                                .description = "Leave V.A.T.S. without attacking. Queued shots are discarded.",
                                .schema = {}};

} // namespace

const Definition &VatsCancelAction::GetDefinition() const { return kDefinition; }

PreparedAction VatsCancelAction::Validate(const Request &request) {
  std::string error;
  if (!ActionData::ValidateNoParameters(request.data, error) || !Menus::VatsHandler::ValidateCancel(error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success([]() { Menus::VatsHandler::StartCancel(); },
                                 []() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!Menus::VatsHandler::ValidateCancel(revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Combat
