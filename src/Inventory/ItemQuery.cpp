#include "Inventory/ItemQuery.hpp"
#include <algorithm>
#include <array>
#include <sstream>
#include <utility>

namespace Inventory {
namespace {

constexpr std::array kQueryTypes = {ItemQueryType::All,          ItemQueryType::Weapons,    ItemQueryType::Equipment,
                                    ItemQueryType::Consumerable, ItemQueryType::WeaponMods, ItemQueryType::Keys,
                                    ItemQueryType::Misc};

} // namespace

bool ParseItemQueryType(std::string_view name, ItemQueryType &type) {
  // The advertised spelling is "Consumerable" (kept for Neuro); the correct spelling is accepted too.
  if (name == "Consumable") {
    type = ItemQueryType::Consumerable;
    return true;
  }
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

std::string QueryTypesHelp() {
  std::string names;
  for (const auto candidate : kQueryTypes) {
    if (!names.empty())
      names += ", ";
    names += ItemQueryTypeName(candidate);
  }
  return "Query types: " + names +
         "; type defaults to All. Weapons includes ammunition; Equipment is armor and clothing; Consumerable is food, "
         "drinks, chems and ingredients; WeaponMods is weapon attachments; Keys is keys; Misc is books, notes, "
         "bottles, junk, crafting components and currency.";
}

size_t PageCount(size_t total) { return std::max(size_t{1}, (total + kItemsPerPage - 1) / kItemsPerPage); }

std::optional<std::string> ValidatePage(size_t total, int page) {
  const size_t pages = PageCount(total);
  // A negative page wraps to a huge value and is refused here as well.
  if (static_cast<size_t>(page) >= pages)
    return "Page index is out of range. Valid pages: 0.." + std::to_string(pages - 1) + ".";
  return std::nullopt;
}

std::string FormatItemPage(ItemQueryType type, int page, size_t total, const std::vector<ListedItem> &items,
                           const char *queryAction) {
  const size_t first = static_cast<size_t>(page) * kItemsPerPage;
  std::ostringstream context;
  context << "Type: " << ItemQueryTypeName(type) << "; page " << page << "; " << total
          << " item stacks. Pages start at 0; item indexes start at 1.\n";
  if (total == 0)
    context << (type == ItemQueryType::All ? "Empty.\n" : "No items match this type.\n");
  for (size_t offset = 0; offset < items.size(); ++offset) {
    context << "\n- `" << first + offset + 1 << "` - " << items[offset].name << " x" << items[offset].count;
    if (items[offset].equipped)
      context << " (equipped)";
  }
  if (first + items.size() < total)
    context << "\nUse " << queryAction << " with index " << page + 1 << " and the same type for the next 15 items.";
  return context.str();
}

} // namespace Inventory
