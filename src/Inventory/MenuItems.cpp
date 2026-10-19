#include "Inventory/MenuItems.hpp"
#include "GameAPI.h"
#include "utils/UIUtils.hpp"
#include <algorithm>
#include <sstream>

namespace Inventory {

ItemIdentity GetItemIdentity(const ItemChange &item) {
  ExtraDataList *extra = nullptr;
  for (auto *node = item.pExtraLists; node && !extra; node = node->GetNext())
    extra = node->GetItem();
  return {item.pObject->GetFormID(), reinterpret_cast<uintptr_t>(extra)};
}

bool MatchesItemIdentity(const ItemChange &item, const ItemIdentity &identity) {
  if (!item.pObject || item.pObject->GetFormID() != identity.formId)
    return false;
  if (!identity.extra)
    return !GetItemIdentity(item).extra;
  for (auto *node = item.pExtraLists; node; node = node->GetNext())
    if (reinterpret_cast<uintptr_t>(node->GetItem()) == identity.extra)
      return true;
  return false;
}

ListBoxItem<ItemChange *> *FindMenuItem(MenuItemEntryList &items, const ItemIdentity &identity, bool *ambiguous) {
  if (ambiguous)
    *ambiguous = false;
  ListBoxItem<ItemChange *> *found = nullptr;
  for (auto *node = items.GetHead(); node; node = node->GetNext()) {
    auto *row = node->GetItem();
    if (!row || !row->tile || !row->object || row->object->iNumber <= 0 || !MatchesItemIdentity(*row->object, identity))
      continue;
    // A base form alone must never select one of several condition/modification stacks.
    if (found) {
      if (ambiguous)
        *ambiguous = true;
      return nullptr;
    }
    found = row;
  }
  return found;
}

size_t CountMenuItems(MenuItemEntryList &items, ItemQueryType type) {
  size_t count = 0;
  for (auto *node = items.GetHead(); node; node = node->GetNext()) {
    auto *row = node->GetItem();
    auto *item = row ? row->object : nullptr;
    if (item && item->iNumber > 0 && MatchesQuery(item->pObject, type))
      ++count;
  }
  return count;
}

std::vector<ListedItem> ReadMenuItemPage(MenuItemEntryList &items, ItemQueryType type, size_t first) {
  std::vector<ListedItem> listed;
  listed.reserve(kItemsPerPage);
  size_t index = 0;
  for (auto *node = items.GetHead(); node && listed.size() < kItemsPerPage; node = node->GetNext()) {
    auto *row = node->GetItem();
    auto *item = row ? row->object : nullptr;
    if (!item || item->iNumber <= 0 || !MatchesQuery(item->pObject, type))
      continue;
    if (index++ < first)
      continue;
    std::string name = UIUtils::GetTileString(row->tile ? row->tile->GetChild("ListItemText") : nullptr);
    if (name.empty())
      name = UIUtils::GetTileString(row->tile);
    if (name.empty())
      name = GetFullName(item->pObject);
    listed.push_back({GetItemIdentity(*item), std::move(name), item->iNumber, item->GetWorn(false)});
  }
  return listed;
}

std::optional<std::string> ValidateMenuPage(MenuItemEntryList &items, int page, ItemQueryType type) {
  if (page < 0)
    return "Page index must be zero or greater.";
  const size_t count = CountMenuItems(items, type);
  const size_t pages = std::max(size_t{1}, (count + kItemsPerPage - 1) / kItemsPerPage);
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
