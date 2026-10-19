#include "Inventory/ItemQuery.hpp"
#include <array>
#include <utility>

namespace Inventory {
namespace {

constexpr std::array kQueryTypes = {ItemQueryType::All,          ItemQueryType::Weapons,    ItemQueryType::Equipment,
                                    ItemQueryType::Consumerable, ItemQueryType::WeaponMods, ItemQueryType::Keys,
                                    ItemQueryType::Misc};

} // namespace

bool ParseItemQueryType(std::string_view name, ItemQueryType &type) {
  for (const auto candidate : kQueryTypes) {
    if (ItemQueryTypeName(candidate) == name) {
      type = candidate;
      return true;
    }
  }
  return false;
}

std::string_view ItemQueryTypeName(ItemQueryType type) {
  switch (type) {
  case ItemQueryType::All:
    return "All";
  case ItemQueryType::Weapons:
    return "Weapons";
  case ItemQueryType::Equipment:
    return "Equipment";
  case ItemQueryType::Consumerable:
    return "Consumerable";
  case ItemQueryType::WeaponMods:
    return "WeaponMods";
  case ItemQueryType::Keys:
    return "Keys";
  case ItemQueryType::Misc:
    return "Misc";
  }
  return {};
}

bool ParseItemQuery(const Actions::ActionData &data, int &page, ItemQueryType &type, std::string &error) {
  if (!data.IsObject()) {
    error = "Expected an object with a non-negative page index and optional item type.";
    return false;
  }
  if (!data.HasProperty("index") || !data.GetInteger("index", page)) {
    error = "index must be a non-negative integer page number.";
    return false;
  }
  if (page < 0) {
    error = "index must be a non-negative integer page number.";
    return false;
  }

  type = ItemQueryType::All;
  if (data.Size() == 1)
    return true;
  if (data.Size() != 2 || !data.HasProperty("type")) {
    error = "Expected only index and optional type fields; duplicate and unexpected fields are not allowed.";
    return false;
  }

  std::string typeName;
  if (!data.GetString("type", typeName)) {
    error = "type must be a string: All, Weapons, Equipment, Consumerable, WeaponMods, Keys, or Misc.";
    return false;
  }
  if (!ParseItemQueryType(typeName, type)) {
    error = "type must be exactly one of All, Weapons, Equipment, Consumerable, WeaponMods, Keys, or Misc.";
    return false;
  }
  return true;
}

Actions::Json::JsonSchema BuildItemQuerySchema() {
  auto schema = Actions::Json::JsonSchema::Object();
  auto index = Actions::Json::JsonSchema::Integer();
  index.Minimum(0);
  schema.Property("index", std::move(index));
  auto type = Actions::Json::JsonSchema::String();
  std::vector<Actions::Json::JsonSchema::Value> choices;
  choices.reserve(kQueryTypes.size());
  for (const auto candidate : kQueryTypes)
    choices.emplace_back(std::string(ItemQueryTypeName(candidate)));
  type.Enum(std::move(choices));
  schema.Property("type", std::move(type), false);
  return schema;
}

bool ParseItemIndex(const Actions::ActionData &data, int &index, std::string &error) {
  if (!data.IsObject() || data.Size() != 1 || !data.HasProperty("index") || !data.GetInteger("index", index)) {
    error = "Expected exactly one integer field: index.";
    return false;
  }
  if (index < 1) {
    error = "index must be a one-based item index of at least 1.";
    return false;
  }
  return true;
}

Actions::Json::JsonSchema BuildItemIndexSchema() {
  auto schema = Actions::Json::JsonSchema::Object();
  auto index = Actions::Json::JsonSchema::Integer();
  index.Minimum(1);
  schema.Property("index", std::move(index));
  return schema;
}

} // namespace Inventory
