#include "SelectPopupOptionAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Menus/MessagePopupHandler.hpp"
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
  return {.name = "choose_popup_option",
          .description = "Choose one of the buttons of the message popup on screen by its zero-based index.",
          .schema = std::move(schema)};
}

} // namespace

SelectPopupOptionAction::SelectPopupOptionAction(size_t optionCount) : m_definition(BuildDefinition(optionCount)) {}

const Definition &SelectPopupOptionAction::GetDefinition() const { return m_definition; }

PreparedAction SelectPopupOptionAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  if (!data->IsObject() || data->Size() != 1)
    return PreparedAction::Failure("Expected exactly one integer property named index.");

  int index = -1;
  if (!data->GetInteger(kIndex, index))
    return PreparedAction::Failure("index must be an integer.");

  Menus::MessagePopupHandler::SelectionSnapshot snapshot;
  if (!Menus::MessagePopupHandler::ValidateSelection(index, snapshot, error))
    return PreparedAction::Failure(std::move(error));

  return PreparedAction::Success([snapshot]() { Menus::MessagePopupHandler::StartExecution(snapshot); },
                                 Revalidator([snapshot](std::string &revalidationError) {
                                   return Menus::MessagePopupHandler::RevalidateSelection(snapshot, revalidationError);
                                 }));
}

} // namespace Actions::Menu
