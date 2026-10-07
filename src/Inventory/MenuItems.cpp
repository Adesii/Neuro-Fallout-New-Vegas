#include "Inventory/MenuItems.hpp"
#include "GameAPI.h"
#include "utils/UIUtils.hpp"

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

} // namespace Inventory
