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

  const ListedItem *Find(int index) const {
    if (!m_available || index < 1 || static_cast<size_t>(index) < m_firstIndex)
      return nullptr;
    const size_t offset = static_cast<size_t>(index) - m_firstIndex;
    return offset < m_items.size() ? &m_items[offset] : nullptr;
  }

  void Consume(int index) {
    if (auto *item = const_cast<ListedItem *>(Find(index)))
      item->consumed = true;
  }

  bool Available() const { return m_available; }
  uint64_t Revision() const { return m_revision; }

private:
  std::vector<ListedItem> m_items;
  size_t m_firstIndex = 1;
  uint64_t m_revision = 0;
  bool m_available = false;
};

} // namespace Inventory
