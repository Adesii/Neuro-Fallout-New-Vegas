#include "SetSpecialAction.hpp"
#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Menus/SpecialAllocationHandler.hpp"
#include "defs/LoveTesterMenu.hpp"
#include <array>
#include <string>
#include <utility>

namespace Actions::Menu {
namespace {

constexpr std::array<const char *, 7> kNames = {"strength",     "perception", "endurance", "charisma",
                                                "intelligence", "agility",    "luck"};

Definition BuildDefinition() {
  auto schema = Actions::Json::JsonSchema::Object();
  for (const char *name : kNames) {
    auto attribute = Actions::Json::JsonSchema::Integer();
    attribute.Minimum(1).Maximum(10);
    schema.Property(name, std::move(attribute));
  }

  return {.name = "set_special",
          .description =
              "Set all seven SPECIAL attributes. Every value must be an integer from 1 to 10 and their total "
              "must equal the available SPECIAL point total.",
          .schema = std::move(schema)};
}

const Definition kDefinition = BuildDefinition();

} // namespace

const Definition &SetSpecialAction::GetDefinition() const { return kDefinition; }

PreparedAction SetSpecialAction::Validate(const Request &request) {
  std::string error;
  auto data = ActionData::Parse(request.data, error);
  if (!data)
    return PreparedAction::Failure(std::move(error));
  if (!data->IsObject() || data->Size() != kNames.size())
    return PreparedAction::Failure(
        "Expected exactly strength, perception, endurance, charisma, intelligence, agility, and luck.");

  Menus::SpecialAllocationHandler::SpecialValues values{};
  for (size_t index = 0; index < kNames.size(); ++index) {
    if (!data->GetInteger(kNames[index], values[index]))
      return PreparedAction::Failure(std::string(kNames[index]) + " must be an integer.");
    if (values[index] < 1 || values[index] > 10)
      return PreparedAction::Failure(std::string(kNames[index]) + " is out of range. Select a value from 1 to 10.");
  }

  if (!Menus::SpecialAllocationHandler::ValidateAllocation(values, error))
    return PreparedAction::Failure(std::move(error));

  const void *owner = LoveTester::GetMenu();
  const int page = LoveTester::GetData()->currentPage;
  const int total = LoveTester::GetData()->totalPoints;
  return PreparedAction::Success(
      "SPECIAL allocation accepted.", [values]() { Menus::SpecialAllocationHandler::StartExecution(values); },
      [values, owner, page, total]() -> std::optional<std::string> {
        std::string revalidationError;
        if (!Menus::SpecialAllocationHandler::RevalidateAllocation(values, owner, page, total, revalidationError))
          return revalidationError;
        return std::nullopt;
      });
}

} // namespace Actions::Menu
