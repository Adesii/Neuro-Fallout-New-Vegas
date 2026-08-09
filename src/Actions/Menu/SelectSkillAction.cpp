#include "SelectSkillAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Menus/CharGenHandler.hpp"
#include <string>
#include <utility>

namespace Actions::Menu {
namespace {

constexpr const char *kSelect = "select";
constexpr const char *kUnselect = "unselect";

Definition BuildDefinition(size_t optionCount) {
  auto schema = Actions::Json::JsonSchema::Object();
  auto select = Actions::Json::JsonSchema::Integer();
  select.Minimum(0).Maximum(static_cast<int>(optionCount - 1));
  schema.Property(kSelect, std::move(select), true);
  auto unselect = Actions::Json::JsonSchema::Integer();
  unselect.Minimum(0).Maximum(static_cast<int>(optionCount - 1));
  schema.Property(kUnselect, std::move(unselect), true);
  return {.name = "select_skill",
          .description = "Tag a skill by index. If all tag slots are occupied, first untag the selected skill at "
                         "unselect; otherwise unselect is ignored but must still be a valid skill index.",
          .schema = std::move(schema)};
}

} // namespace

SelectSkillAction::SelectSkillAction(size_t optionCount) : m_definition(BuildDefinition(optionCount)) {}

const Definition &SelectSkillAction::GetDefinition() const { return m_definition; }

PreparedAction SelectSkillAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  if (!data->IsObject() || data->Size() != 2)
    return PreparedAction::Failure("Expected exactly the integer properties select and unselect.");

  int select = -1;
  int unselect = -1;
  if (!data->GetInteger(kSelect, select) || !data->GetInteger(kUnselect, unselect))
    return PreparedAction::Failure("select and unselect must both be integers.");

  Menus::CharGenHandler::SelectionSnapshot snapshot;
  if (!Menus::CharGenHandler::ValidateSelection(select, unselect, snapshot, error))
    return PreparedAction::Failure(std::move(error));
  return PreparedAction::Success(
      "Skill selection accepted.", [snapshot]() { Menus::CharGenHandler::StartSelection(snapshot); },
      [snapshot]() -> std::optional<std::string> {
        std::string revalidationError;
        if (!Menus::CharGenHandler::RevalidateSelection(snapshot, revalidationError))
          return revalidationError;
        return std::nullopt;
      });
}

} // namespace Actions::Menu
