#pragma once

#include "GameUI.h"
#include "Inventory/ItemListing.hpp"
#include "Inventory/ItemQuery.hpp"
#include <optional>
#include <string>

namespace Inventory {

ItemIdentity GetItemIdentity(const ItemChange &item);
bool MatchesItemIdentity(const ItemChange &item, const ItemIdentity &identity);
ListBoxItem<ItemChange *> *FindMenuItem(MenuItemEntryList &items, const ItemIdentity &identity,
                                        bool *ambiguous = nullptr);
size_t CountMenuItems(MenuItemEntryList &items, ItemQueryType type);
std::vector<ListedItem> ReadMenuItemPage(MenuItemEntryList &items, ItemQueryType type, size_t first);
std::optional<std::string> ValidateMenuPage(MenuItemEntryList &items, int page, ItemQueryType type);
// Formats a published page: type/count header, numbered stacks and the next-page hint for queryAction.
std::string FormatItemPage(ItemQueryType type, int page, size_t total, const std::vector<ListedItem> &items,
                           const char *queryAction);

} // namespace Inventory
