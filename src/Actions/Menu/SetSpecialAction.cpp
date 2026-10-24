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

constexpr std::array<const char *, Menus::Special::kAttributeCount> kNames = {
    "strength", "perception", "endurance", "charisma", "intelligence", "agility", "luck"};

Definition BuildDefinition() {
  auto schema = Actions::Json::JsonSchema::Object();
  for (const char *name : kNames) {
    auto attribute = Actions::Json::JsonSchema::Integer();
    attribute.Minimum(Menus::Special::kMinValue).Maximum(Menus::Special::kMaxValue);
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
    if (values[index] < Menus::Special::kMinValue || values[index] > Menus::Special::kMaxValue)
      return PreparedAction::Failure(std::string(kNames[index]) + " is out of range. Select a value from " +
                                     std::to_string(Menus::Special::kMinValue) + " to " +
                                     std::to_string(Menus::Special::kMaxValue) + ".");
  }

  if (!Menus::SpecialAllocationHandler::ValidateAllocation(values, error))
    return PreparedAction::Failure(std::move(error));

  const void *owner = LoveTester::GetMenu();
  const int page = LoveTester::GetData()->currentPage;
  const int total = LoveTester::GetData()->totalPoints;
  return PreparedAction::Success([values]() { Menus::SpecialAllocationHandler::StartExecution(values); },
                                 Revalidator([values, owner, page, total](std::string &revalidationError) {
                                   return Menus::SpecialAllocationHandler::RevalidateAllocation(
                                       values, owner, page, total, revalidationError);
                                 }));
}

} // namespace Actions::Menu
