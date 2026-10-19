#pragma once

#include "GameUI.h"
#include "Inventory/ItemListing.hpp"
#include "Inventory/ItemQuery.hpp"

namespace Inventory {

ItemIdentity GetItemIdentity(const ItemChange &item);
bool MatchesItemIdentity(const ItemChange &item, const ItemIdentity &identity);
ListBoxItem<ItemChange *> *FindMenuItem(MenuItemEntryList &items, const ItemIdentity &identity,
                                        bool *ambiguous = nullptr);
size_t CountMenuItems(MenuItemEntryList &items, ItemQueryType type);
std::vector<ListedItem> ReadMenuItemPage(MenuItemEntryList &items, ItemQueryType type, size_t first);

} // namespace Inventory
