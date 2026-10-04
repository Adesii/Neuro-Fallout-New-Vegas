#include "ContainerHandler.hpp"
#include "Actions/Container/LootAllAction.hpp"
#include "Actions/Container/LootItemAction.hpp"
#include "Actions/Container/QueryItemsAction.hpp"
#include "Actions/Container/QueryOwnItemsAction.hpp"
#include "Actions/Container/StowAwayItemAction.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "GameAPI.h"
#include "GameObjects.h"
#include "GameUI.h"
#include "Inventory/ItemListing.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/UIUtils.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <sstream>

namespace Menus::ContainerHandler {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kStepDelay = std::chrono::milliseconds(350);
constexpr auto kTransferTimeout = std::chrono::seconds(60);

struct Transfer {
  Inventory::ItemIdentity identity;
  std::string name;
  int count = 0;
  bool stow = false;
  bool all = false;
  enum class Phase { Select, Click, Confirm, Observe } phase = Phase::Select;
  Clock::time_point startedAt{};
  Clock::time_point nextStepAt{};
};

Actions::PersistentActionSet g_actions;
bool g_actionsBuilt = false;
ContainerMenu *g_owner = nullptr;
uint32_t g_containerId = 0;
uint64_t g_session = 0;
bool g_announced = false;
Clock::time_point g_openedAt;
Inventory::ItemListing g_containerItems;
Inventory::ItemListing g_ownItems;
std::deque<Transfer> g_transfers;

bool IsActive() {
  return Interface::GetTopMenuID() == Interface::Container && g_owner && ContainerMenu::GetSingleton() == g_owner &&
         g_owner->containerRef && g_owner->containerRef->GetFormID() == g_containerId;
}

MenuItemEntryList &Items(bool own) { return own ? g_owner->leftItems : g_owner->rightItems; }
Inventory::ItemListing &Listing(bool own) { return own ? g_ownItems : g_containerItems; }

Inventory::ItemIdentity Identity(ItemChange *item) {
  ExtraDataList *extra = nullptr;
  for (auto *node = item->pExtraLists; node && !extra; node = node->GetNext())
    extra = node->GetItem();
  return {item->pObject->GetFormID(), reinterpret_cast<uintptr_t>(extra)};
}

bool MatchesIdentity(ItemChange *item, const Inventory::ItemIdentity &identity) {
  if (item->pObject->GetFormID() != identity.formId)
    return false;
  if (!identity.extra)
    return !Identity(item).extra;
  for (auto *node = item->pExtraLists; node; node = node->GetNext())
    if (reinterpret_cast<uintptr_t>(node->GetItem()) == identity.extra)
      return true;
  return false;
}

ListBoxItem<ItemChange *> *FindLive(bool own, const Inventory::ItemIdentity &identity, bool *ambiguous = nullptr) {
  ListBoxItem<ItemChange *> *found = nullptr;
  for (auto *node = Items(own).GetHead(); node; node = node->GetNext()) {
    auto *row = node->GetItem();
    if (!row || !row->tile || !row->object || !row->object->pObject || row->object->iNumber <= 0 ||
        !MatchesIdentity(row->object, identity))
      continue;
    // Ambiguous stacks are never resolved by a shifting index or by base form alone.
    if (found) {
      if (ambiguous)
        *ambiguous = true;
      return nullptr;
    }
    found = row;
  }
  return found;
}

size_t CountItems(bool own, Inventory::ItemQueryType type) {
  size_t count = 0;
  for (auto *node = Items(own).GetHead(); node; node = node->GetNext()) {
    auto *row = node->GetItem();
    auto *item = row ? row->object : nullptr;
    if (item && item->pObject && item->iNumber > 0 && Inventory::MatchesQuery(item->pObject, type))
      ++count;
  }
  return count;
}

std::vector<Inventory::ListedItem> ReadPage(bool own, Inventory::ItemQueryType type, size_t first) {
  std::vector<Inventory::ListedItem> items;
  items.reserve(Inventory::kItemsPerPage);
  size_t index = 0;
  for (auto *node = Items(own).GetHead(); node && items.size() < Inventory::kItemsPerPage; node = node->GetNext()) {
    auto *row = node->GetItem();
    auto *item = row ? row->object : nullptr;
    if (!item || !item->pObject || item->iNumber <= 0 || !Inventory::MatchesQuery(item->pObject, type))
      continue;
    if (index++ < first)
      continue;
    std::string name = UIUtils::GetTileString(row->tile ? row->tile->GetChild("ListItemText") : nullptr);
    if (name.empty())
      name = GetFullName(item->pObject);
    items.push_back({Identity(item), std::move(name), item->iNumber, item->GetWorn(false)});
  }
  return items;
}

std::optional<std::string> ValidateSession(uint64_t session) {
  if (session != g_session || !IsActive())
    return "The looting container is no longer the active menu. Open a container first.";
  return std::nullopt;
}

std::optional<std::string> ValidatePage(bool own, int page, Inventory::ItemQueryType type) {
  if (page < 0)
    return "Page index must be zero or greater.";
  const size_t count = CountItems(own, type);
  const size_t pages = std::max(size_t{1}, (count + Inventory::kItemsPerPage - 1) / Inventory::kItemsPerPage);
  if (static_cast<size_t>(page) >= pages)
    return "Page index is out of range. Valid pages: 0.." + std::to_string(pages - 1) + ".";
  return std::nullopt;
}

bool PublishItems(bool own, int page, Inventory::ItemQueryType type, bool opening = false) {
  const size_t count = CountItems(own, type);
  const size_t first = static_cast<size_t>(page) * Inventory::kItemsPerPage;
  auto listed = ReadPage(own, type, first);
  const size_t end = first + listed.size();
  std::ostringstream context;
  context << "## " << (own ? "Your inventory" : "Container contents");
  if (!own)
    context << ": "
            << UIUtils::GetTileString(UIUtils::GetControl(g_owner, "NOGLOW_BRANCH/CM_ContainerRect/CM_ContainerTitle"));
  context << "\nType: " << Inventory::ItemQueryTypeName(type) << "; page " << page << "; " << count
          << " item stacks. Pages start at 0; item indexes start at 1.\n";
  if (count == 0)
    context << (type == Inventory::ItemQueryType::All ? "Empty.\n" : "No items match this type.\n");
  for (size_t offset = 0; offset < listed.size(); ++offset) {
    context << "\n- `" << first + offset + 1 << "` - " << listed[offset].name << " x" << listed[offset].count;
    if (listed[offset].equipped)
      context << " (equipped)";
  }
  if (end < count)
    context << "\nUse " << (own ? "query_own_items" : "query_items") << " with index " << page + 1
            << " and the same type for the next 15 items.";
  context << "\n"
          << (own ? "stow_away_item" : "loot_item")
          << " uses indexes from this listing and transfers the entire stack. Indexes stay fixed until your next "
          << (own ? "query_own_items" : "query_items") << " or until the container closes.";
  if (opening)
    context << "\nContainer actions: loot_item, query_items, loot_all, query_own_items, stow_away_item. "
               "Query your own items before stowing anything. Query types: All, Weapons, Equipment, Consumerable, "
               "WeaponMods, "
               "Keys, Misc; type defaults to All. Weapons includes ammunition; Equipment is armor and clothing; "
               "Consumerable is food, drinks, chems and ingredients; WeaponMods is weapon attachments; Keys is keys; "
               "Misc is books, notes, bottles, junk, crafting components and currency.";
  if (!NeuroSDK::SendContext(context.str().c_str(), true))
    return false;
  Listing(own).Replace(std::move(listed), first + 1);
  return true;
}

void BuildActions() {
  if (g_actionsBuilt)
    return;
  g_actions.Add(std::make_unique<Actions::Container::LootItemAction>())
      .Add(std::make_unique<Actions::Container::QueryItemsAction>())
      .Add(std::make_unique<Actions::Container::LootAllAction>())
      .Add(std::make_unique<Actions::Container::QueryOwnItemsAction>())
      .Add(std::make_unique<Actions::Container::StowAwayItemAction>());
  g_actionsBuilt = true;
}

void StopTransfers(const std::string &reason) {
  _WARNING("Container transfer stopped: %s", reason.c_str());
  NeuroSDK::SendContext(("## Container transfer stopped\n" + reason + " Query items again before retrying.").c_str());
  g_transfers.clear();
}

bool ControlAvailable(Tile *tile) {
  if (!tile)
    return false;
  auto *visible = tile->GetValue(kTileValue_visible);
  auto *enabled = tile->GetValue(kTileValue_target);
  return (!visible || visible->num != 0.0f) && (!enabled || enabled->num != 0.0f);
}

int ContainerCount() {
  int count = 0;
  for (auto *node = g_owner->rightItems.GetHead(); node; node = node->GetNext()) {
    auto *row = node->GetItem();
    if (row && row->object && row->object->iNumber > 0)
      count += row->object->iNumber;
  }
  return count;
}

void FinishTransfer() {
  const auto &transfer = g_transfers.front();
  const std::string context = transfer.all
                                  ? "## Container loot\nTake All completed; the container is empty."
                                  : "## Container transfer\n" + std::string(transfer.stow ? "Stowed " : "Looted ") +
                                        transfer.name + " x" + std::to_string(transfer.count) + ".";
  NeuroSDK::SendContext(context.c_str(), true);
  g_transfers.pop_front();
}

void AdvanceTransfer() {
  if (g_transfers.empty())
    return;
  auto &transfer = g_transfers.front();
  const auto now = Clock::now();
  const bool quantityActive = Interface::GetTopMenuID() == Interface::Quantity;
  if (!IsActive() && !quantityActive)
    return; // A warning or another popup owns its own controls.
  if (transfer.startedAt == Clock::time_point{}) {
    transfer.startedAt = now;
    transfer.nextStepAt = now + kStepDelay;
  }
  if (now - transfer.startedAt > kTransferTimeout) {
    if (quantityActive) {
      auto *manager = InterfaceManager::GetSingleton();
      auto *menu = manager ? manager->activeMenu : nullptr;
      if (menu && menu->GetID() == Interface::Quantity && transfer.phase == Transfer::Phase::Confirm)
        UIUtils::ClickControl(menu, "NOGLOW_BRANCH/QM_MainRect/QM_CancelButton");
    }
    StopTransfers("The visible transfer timed out or the game refused the item.");
    return;
  }
  if (now < transfer.nextStepAt)
    return;
  transfer.nextStepAt = now + kStepDelay;

  if (quantityActive) {
    if (transfer.all || transfer.phase != Transfer::Phase::Confirm)
      return; // Never confirm a quantity menu that this transfer did not open.
    auto *manager = InterfaceManager::GetSingleton();
    auto *active = manager ? manager->activeMenu : nullptr;
    if (!active || active->GetID() != Interface::Quantity)
      return;
    auto *quantity = static_cast<QuantityMenu *>(active);
    auto *maximum = quantity->tile28 ? quantity->tile28->GetValue(kTileValue_user0) : nullptr;
    if (!maximum || maximum->num != static_cast<float>(transfer.count)) {
      UIUtils::ClickControl(quantity, "NOGLOW_BRANCH/QM_MainRect/QM_CancelButton");
      StopTransfers("The quantity prompt no longer matches the selected stack.");
      return;
    }
    if (quantity->currentQtt < maximum->num) {
      UIUtils::ClickControl(quantity, "NOGLOW_BRANCH/QM_MainRect/QM_IncreaseArrow");
      transfer.nextStepAt = now + std::chrono::milliseconds(80);
      return;
    }
    if (quantity->currentQtt != maximum->num) {
      StopTransfers("The quantity prompt has an invalid selected amount.");
      return;
    }
    if (!UIUtils::ClickControl(quantity, "NOGLOW_BRANCH/QM_MainRect/QM_OKButton")) {
      StopTransfers("The quantity confirmation control is unavailable.");
      return;
    }
    transfer.phase = Transfer::Phase::Observe;
    return;
  }

  if (transfer.all) {
    if (transfer.phase == Transfer::Phase::Observe) {
      if (ContainerCount() == 0)
        FinishTransfer();
      return;
    }
    // Take All uses the same native control as the keyboard A shortcut.
    auto *control = UIUtils::GetControl(g_owner, "NOGLOW_BRANCH/CM_ButtonRect/CM_TakeAllButton");
    if (!ControlAvailable(control) || !UIUtils::ClickTile(g_owner, control)) {
      StopTransfers("Take All is unavailable in this container.");
      return;
    }
    transfer.phase = Transfer::Phase::Observe;
    return;
  }

  bool ambiguous = false;
  auto *row = FindLive(transfer.stow, transfer.identity, &ambiguous);
  if (ambiguous) {
    StopTransfers("The selected stack became ambiguous; completion cannot be confirmed.");
    return;
  }
  if (transfer.phase == Transfer::Phase::Observe || transfer.phase == Transfer::Phase::Confirm) {
    if (!row) {
      FinishTransfer();
      return;
    }
    // A partial transfer is not success for a whole-stack request.
    if (row->object->iNumber != transfer.count) {
      StopTransfers("The stack changed without completing the whole-stack transfer.");
      return;
    }
    return;
  }
  if (!row) {
    StopTransfers("The selected stack disappeared or became ambiguous.");
    return;
  }
  const auto filter = transfer.stow ? g_owner->leftFilter : g_owner->rightFilter;
  if (filter != 0) {
    const char *arrow = transfer.stow ? "NOGLOW_BRANCH/CM_ItemsRect/CM_Items_LeftFilterArrow"
                                      : "NOGLOW_BRANCH/CM_ContainerRect/CM_Container_LeftFilterArrow";
    if (!UIUtils::ClickControl(g_owner, arrow))
      StopTransfers("The source pane's All-items filter could not be selected.");
    return;
  }
  auto &list = Items(transfer.stow);
  if (g_owner->currentItems != &list) {
    // JIP-LN-NVSE nvse/GameUI.h documents vtable slot 0x38 as
    // HandleSpecialKeyInput(code, float). Included JG calls it Unk_0E;
    // passing integer zero has the identical x86 stack representation to 0.0f.
    // Codes 13/14 are the native Shift+Left/Shift+Right pane shortcuts.
    g_owner->Unk_0E(transfer.stow ? 13 : 14, 0);
    return;
  }
  if (row->byte08) {
    StopTransfers("The item is hidden by the current menu or search filter.");
    return;
  }
  if (transfer.phase == Transfer::Phase::Select) {
    if (row->object->iNumber != transfer.count) {
      StopTransfers("The listed stack quantity changed before it could be selected.");
      return;
    }
    list.SetSelectedTile(row->tile);
    list.ScrollToHighlight();
    transfer.phase = Transfer::Phase::Click;
    return;
  }
  if (!ControlAvailable(row->tile)) {
    StopTransfers("The selected item is not visible or actionable after scrolling.");
    return;
  }
  if (list.GetSelectedTile() != row->tile || row->object->iNumber != transfer.count) {
    StopTransfers("The visible item selection changed before transfer.");
    return;
  }
  if (!UIUtils::ClickTile(g_owner, row->tile)) {
    StopTransfers("The selected item could not be activated.");
    return;
  }
  transfer.phase = Transfer::Phase::Confirm;
}

} // namespace

void Reset() {
  g_actions.Unregister();
  g_owner = nullptr;
  g_containerId = 0;
  ++g_session;
  g_announced = false;
  g_containerItems.Clear();
  g_ownItems.Clear();
  g_transfers.clear();
}

bool Process() {
  auto *menu = ContainerMenu::GetSingleton();
  const bool visible = Menu::IsMenuVisible(Interface::Container);
  if (!visible || !menu || !menu->containerRef) {
    if (g_owner) {
      if (!g_transfers.empty()) {
        if (g_transfers.front().all && g_transfers.front().phase == Transfer::Phase::Observe)
          NeuroSDK::SendContext("## Container loot\nTake All was activated and the container menu closed.", true);
        else
          StopTransfers("The container closed before the remaining transfers could be confirmed.");
      }
      Reset();
    } else {
      g_actions.Unregister();
    }
    return false;
  }
  if (menu != g_owner || menu->containerRef->GetFormID() != g_containerId) {
    if (!g_transfers.empty())
      StopTransfers("A different container was opened.");
    Reset();
    g_owner = menu;
    g_containerId = menu->containerRef->GetFormID();
    g_openedAt = Clock::now();
  }
  AdvanceTransfer();
  if (!IsActive()) {
    g_actions.Unregister();
    return !g_transfers.empty();
  }
  BuildActions();
  if (!g_announced) {
    // Wait a visible frame for native inventory population before announcing an empty container.
    if (Clock::now() - g_openedAt < kStepDelay)
      return true;
    g_announced = PublishItems(false, 0, Inventory::ItemQueryType::All, true);
  }
  if (g_announced)
    g_actions.Register();
  return true;
}

Actions::PreparedAction PrepareQuery(bool own, int page, Inventory::ItemQueryType type) {
  const uint64_t session = g_session;
  auto revalidate = [session, own, page, type]() -> std::optional<std::string> {
    if (auto error = ValidateSession(session))
      return error;
    if (!g_transfers.empty())
      return "Wait for the accepted transfers to finish before querying a new item listing.";
    return ValidatePage(own, page, type);
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  return Actions::PreparedAction::Success([own, page, type] { PublishItems(own, page, type); }, revalidate);
}

Actions::PreparedAction PrepareTransfer(bool stow, int index) {
  const uint64_t session = g_session;
  auto &listing = Listing(stow);
  if (auto error = ValidateSession(session))
    return Actions::PreparedAction::Failure(*error);
  if (!listing.Available())
    return Actions::PreparedAction::Failure(stow ? "Call query_own_items first before using stow_away_item."
                                                 : "Call query_items first before using loot_item.");
  auto *item = listing.Find(index);
  if (!item)
    return Actions::PreparedAction::Failure(
        stow ? "Index is not in your last inventory listing. Call query_own_items first."
             : "Index is not in the last container listing. Call query_items first; only the first 15 indexes are "
               "listed on opening.");
  const auto identity = item->identity;
  const int count = item->count;
  const uint64_t revision = listing.Revision();
  auto revalidate = [session, stow, index, revision, identity, count]() -> std::optional<std::string> {
    if (auto error = ValidateSession(session))
      return error;
    auto &listing = Listing(stow);
    if (listing.Revision() != revision)
      return "The item listing changed. Use the indexes in the latest query.";
    auto *item = listing.Find(index);
    if (!item || item->consumed)
      return "That listed stack has already been accepted for transfer. Choose another index or query again.";
    auto *live = FindLive(stow, identity);
    if (!live)
      return "That stack is no longer available or cannot be identified uniquely. Query items again.";
    if (live->object->iNumber != count)
      return "That stack's quantity changed. Query items again before transferring it.";
    if (std::any_of(g_transfers.begin(), g_transfers.end(), [](const Transfer &transfer) { return transfer.all; }))
      return "Loot all is already in progress. Wait for it to finish.";
    return std::nullopt;
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  const std::string name = item->name;
  return Actions::PreparedAction::Success(
      [stow, index, identity, name, count] {
        Listing(stow).Consume(index);
        g_transfers.push_back({.identity = identity, .name = name, .count = count, .stow = stow});
      },
      revalidate);
}

Actions::PreparedAction PrepareLootAll() {
  const uint64_t session = g_session;
  auto revalidate = [session]() -> std::optional<std::string> {
    if (auto error = ValidateSession(session))
      return error;
    if (!g_transfers.empty())
      return "Wait for the accepted item transfers to finish before using loot_all.";
    if (!ControlAvailable(UIUtils::GetControl(g_owner, "NOGLOW_BRANCH/CM_ButtonRect/CM_TakeAllButton")))
      return "Take All is not available in this container.";
    return std::nullopt;
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  return Actions::PreparedAction::Success([] { g_transfers.push_back({.all = true}); }, revalidate);
}

} // namespace Menus::ContainerHandler
