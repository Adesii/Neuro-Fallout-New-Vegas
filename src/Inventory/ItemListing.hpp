#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Inventory {

inline constexpr size_t kItemsPerPage = 15;

// Engine row/tile objects are rebuilt after transfers. Only the form and persistent
// per-instance extra data identify a stack; never retain a dereferenceable row pointer.
struct ItemIdentity {
  uint32_t formId = 0;
  uintptr_t extra = 0;
  bool operator==(const ItemIdentity &) const = default;
};

struct ListedItem {
  ItemIdentity identity;
  std::string name;
  int count = 0;
  bool equipped = false;
  bool consumed = false;
};

class ItemListing {
public:
  void Replace(std::vector<ListedItem> items, size_t firstIndex) {
    m_items = std::move(items);
    m_firstIndex = firstIndex;
    m_available = true;
    ++m_revision;
  }

  void Clear() {
    m_items.clear();
    m_available = false;
    ++m_revision;
  }

  const ListedItem *Find(int index) const { return Slot(index); }

  void Consume(int index) {
    if (ListedItem *item = Slot(index))
      item->consumed = true;
  }

  bool Available() const { return m_available; }
  uint64_t Revision() const { return m_revision; }

private:
  // The published stack with this one-based index, or null when it is not on the published page.
  ListedItem *Slot(int index) {
    if (!m_available || index < 1 || static_cast<size_t>(index) < m_firstIndex)
      return nullptr;
    const size_t offset = static_cast<size_t>(index) - m_firstIndex;
    return offset < m_items.size() ? &m_items[offset] : nullptr;
  }
  const ListedItem *Slot(int index) const { return const_cast<ItemListing *>(this)->Slot(index); }

  std::vector<ListedItem> m_items;
  size_t m_firstIndex = 1;
  uint64_t m_revision = 0;
  bool m_available = false;
};

} // namespace Inventory
