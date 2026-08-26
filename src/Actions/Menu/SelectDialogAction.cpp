#include "SelectDialogAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Menus/DialogHandler.hpp"
#include <string>
#include <utility>

namespace Actions::Menu {
namespace {

constexpr const char *kIndex = "index";

Definition BuildDefinition(size_t optionCount) {
  auto schema = Actions::Json::JsonSchema::Object();
  auto index = Actions::Json::JsonSchema::Integer();
  index.Minimum(0).Maximum(static_cast<int>(optionCount - 1));
  schema.Property(kIndex, std::move(index), true);
  return {.name = "select_dialog",
          .description = "Select one of the currently displayed dialog responses by its zero-based index.",
          .schema = std::move(schema)};
}

} // namespace

SelectDialogAction::SelectDialogAction(size_t optionCount) : m_definition(BuildDefinition(optionCount)) {}

const Definition &SelectDialogAction::GetDefinition() const { return m_definition; }

PreparedAction SelectDialogAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  if (!data->IsObject() || data->Size() != 1)
    return PreparedAction::Failure("Expected exactly one integer property named index.");

  int index = -1;
  if (!data->GetInteger(kIndex, index))
    return PreparedAction::Failure("index must be an integer.");

  Menus::DialogHandler::SelectionSnapshot snapshot;
  if (!Menus::DialogHandler::ValidateSelection(index, snapshot, error))
    return PreparedAction::Failure(std::move(error));

  return PreparedAction::Success([snapshot]() { Menus::DialogHandler::StartExecution(snapshot); },
                                 [snapshot]() -> std::optional<std::string> {
                                   std::string revalidationError;
                                   if (!Menus::DialogHandler::RevalidateSelection(snapshot, revalidationError))
                                     return revalidationError;
                                   return std::nullopt;
                                 });
}

} // namespace Actions::Menu
