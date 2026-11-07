#pragma once

#include "Actions/ActionData.hpp"
#include "Actions/Json/JsonSchema.hpp"
#include "Inventory/ItemListing.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class TESForm;

namespace Inventory {

enum class ItemQueryType { All, Weapons, Equipment, Consumerable, WeaponMods, Keys, Misc };

bool ParseItemQueryType(std::string_view name, ItemQueryType &type);
std::string_view ItemQueryTypeName(ItemQueryType type);
bool ParseItemQuery(const Actions::ActionData &data, int &page, ItemQueryType &type, std::string &error);
Actions::Json::JsonSchema BuildItemQuerySchema();
bool ParseItemIndex(const Actions::ActionData &data, int &index, std::string &error);
Actions::Json::JsonSchema BuildItemIndexSchema();

// A "Query types: ..." sentence built from the same table the schema and the parser use, so it cannot drift from what
// is accepted. Currently unused: listings carry state only, and how to query belongs in the action descriptions.
std::string QueryTypesHelp();

// Pages hold kItemsPerPage stacks; an empty list still has page 0.
size_t PageCount(size_t total);
// Null when `page` (zero-based; the parser already guarantees it is not negative) exists for `total` stacks, otherwise
// the correction message naming the valid pages.
std::optional<std::string> ValidatePage(size_t total, int page);
// Formats a published page as state only: filter (when not All), stack count, the page range when the listing spans
// several pages, and the numbered stacks. It never explains how to use the actions.
std::string FormatItemPage(ItemQueryType type, int page, size_t total, const std::vector<ListedItem> &items);

ItemQueryType ClassifyItem(const TESForm &form);
bool MatchesQuery(const TESForm *form, ItemQueryType type);

} // namespace Inventory
