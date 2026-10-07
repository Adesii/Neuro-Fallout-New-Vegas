#include "InventoryHandler.hpp"
#include "Actions/Inventory/DropItemAction.hpp"
#include "Actions/Inventory/EquipItemAction.hpp"
#include "Actions/Inventory/QueryOwnItemsAction.hpp"
#include "Actions/Inventory/UnequipItemAction.hpp"
#include "Actions/Inventory/UseItemAction.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "Inventory/MenuItems.hpp"
#include "Menus/PipBoyHandler.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/UIUtils.hpp"
#include <algorithm>
#include <deque>
#include <memory>
#include <sstream>

namespace Menus::InventoryHandler {
namespace {

using PipBoyHandler::Clock;
using PipBoyHandler::kStepDelay;
constexpr auto kExecutionTimeout = std::chrono::seconds(60);

struct StackState {
  Inventory::ItemIdentity identity;
  int count = 0;
  bool equipped = false;
};

struct Execution {
  enum class Phase { Read, Navigate, Select, Activate, Confirm, Observe, Publish } phase = Phase::Navigate;
  bool query = false;
  bool opening = false;
  bool keyringSelected = false;
  bool quantityObserved = false;
  Menu *quantityOwner = nullptr;
  Operation operation = Operation::Equip;
  Inventory::ItemIdentity identity;
  std::string name;
  int count = 0;
  uint32_t category = 0;
  int page = 0;
  Inventory::ItemQueryType type = Inventory::ItemQueryType::All;
  std::vector<Inventory::ListedItem> items;
  size_t total = 0;
  std::vector<StackState> before;
  Clock::time_point startedAt{};
  Clock::time_point nextStepAt{};
};

Actions::PersistentActionSet g_actions;
bool g_actionsBuilt = false;
InventoryMenu *g_owner = nullptr;
uint64_t g_session = 0;
bool g_initialAttempted = false;
Clock::time_point g_openedAt;
Inventory::ItemListing g_listing;
std::deque<Execution> g_queue;

bool IsActive() {
  return g_owner && InventoryMenu::GetSingleton() == g_owner && PipBoyHandler::IsActive(PipBoyHandler::Tab::Items) &&
         !PipBoyHandler::IsTransitioning();
}

bool ControlAvailable(Tile *tile) {
  if (!tile)
    return false;
  auto *visible = tile->GetValue(kTileValue_visible);
  auto *enabled = tile->GetValue(kTileValue_target);
  return (!visible || visible->num != 0.0f) && (!enabled || enabled->num != 0.0f);
}

bool Highlight(Tile *tile) {
  auto *id = tile ? tile->GetValue(kTileValue_id) : nullptr;
  if (!id)
    return false;
  const auto tileId = static_cast<uint32_t>(static_cast<int32_t>(id->num));
  g_owner->itemList.SetSelectedTile(tile);
  g_owner->itemList.ScrollToHighlight();
  // Notify the native menu as a mouseover would, updating its selected entry
  // and item card before the later activation. Menu's first parameter is tileID.
  g_owner->HandleMouseover(tileId, tile);
  return g_owner->itemList.GetSelectedTile() == tile;
}

ListBoxItem<ItemChange *> *FindLive(const Inventory::ItemIdentity &identity, bool *ambiguous = nullptr) {
  return Inventory::FindMenuItem(g_owner->itemList, identity, ambiguous);
}

// Native category order is corroborated by JIP's InventoryMenu::tabScrollPos
// and FalloutNVAccess's InventoryMenuHandler. Shared API categories stay unchanged.
uint32_t NativeCategory(const TESForm &form) {
  if (form.eFormType == _FormType::TESAmmo)
    return 4;
  switch (Inventory::ClassifyItem(form)) {
  case Inventory::ItemQueryType::Weapons:
    return 0;
  case Inventory::ItemQueryType::Equipment:
    return 1;
  case Inventory::ItemQueryType::Consumerable:
    return 2;
  case Inventory::ItemQueryType::Keys:
    return 5;
  default:
    return 3;
  }
}

uint32_t EmptyQueryCategory(Inventory::ItemQueryType type) {
  switch (type) {
  case Inventory::ItemQueryType::All:
    return g_owner->filter < 5 ? g_owner->filter : 3;
  case Inventory::ItemQueryType::Weapons:
    return 0;
  case Inventory::ItemQueryType::Equipment:
    return 1;
  case Inventory::ItemQueryType::Consumerable:
    return 2;
  default:
    return 3;
  }
}

std::optional<std::string> ValidateSession(uint64_t session) {
  if (session != g_session || !IsActive())
    return "Your inventory is no longer the active Pip-Boy tab. Open inventory or finish the current popup first.";
  return std::nullopt;
}

std::optional<std::string> ValidatePage(int page, Inventory::ItemQueryType type) {
  if (page < 0)
    return "Page index must be zero or greater.";
  const size_t count = Inventory::CountMenuItems(g_owner->itemList, type);
  const size_t pages = std::max(size_t{1}, (count + Inventory::kItemsPerPage - 1) / Inventory::kItemsPerPage);
  if (static_cast<size_t>(page) >= pages)
    return "Page index is out of range. Valid pages: 0.." + std::to_string(pages - 1) + ".";
  return std::nullopt;
}

std::optional<std::string> ValidateOperation(Operation operation, const ItemChange &item) {
  const auto &form = *item.pObject;
  if (operation == Operation::Equip || operation == Operation::Unequip) {
    if (form.eFormType != _FormType::TESObjectWEAP && form.eFormType != _FormType::TESObjectARMO &&
        form.eFormType != _FormType::TESObjectCLOT)
      return "Only weapons, armor and clothing can be equipped or unequipped. Ammunition and weapon mods cannot.";
  } else if (operation == Operation::Use) {
    if (Inventory::ClassifyItem(form) != Inventory::ItemQueryType::Consumerable)
      return "use_item requires an aid item: food, drink, chem or ingredient.";
    if (form.GetQuestObject())
      return "This quest item cannot be consumed.";
  } else {
    if (form.GetQuestObject())
      return "This quest item cannot be dropped.";
    if (Inventory::ClassifyItem(form) == Inventory::ItemQueryType::Keys)
      return "Keys in the native keyring cannot be dropped.";
  }
  return std::nullopt;
}

void StopExecution(const std::string &reason) {
  _WARNING("Inventory execution stopped: %s", reason.c_str());
  NeuroSDK::SendContext(
      ("## Inventory action stopped\n" + reason + " Query your inventory again before retrying.").c_str());
  g_queue.clear();
  g_listing.Clear();
}

Tile *CategoryButton(uint32_t category) {
  auto *tabs = g_owner->tile05C;
  Tile *found = nullptr;
  for (auto *node = tabs ? tabs->children.m_pkHead : nullptr; node; node = node->m_pkNext) {
    auto *tile = node->m_element;
    auto *index = tile ? tile->GetValue(kTileValue_listindex) : nullptr;
    if (!index || index->num != static_cast<float>(category) || !tile->GetValue(kTileValue_id))
      continue;
    if (found)
      return nullptr;
    found = tile;
  }
  return found;
}

enum class Navigation { Ready, Waiting, Failed };

Navigation Navigate(Execution &execution, std::string &error) {
  if (g_owner->filter == execution.category)
    return Navigation::Ready;
  if (g_owner->filter == 5) {
    if (!UIUtils::ClickTile(g_owner, g_owner->tile050)) {
      error = "The native keyring could not be closed.";
      return Navigation::Failed;
    }
    execution.keyringSelected = false;
    return Navigation::Waiting;
  }

  const uint32_t category = execution.category == 5 ? 3 : execution.category;
  if (g_owner->filter != category) {
    auto *button = CategoryButton(category);
    if (!ControlAvailable(button) || !UIUtils::ClickTile(g_owner, button)) {
      error = "The native inventory category control is unavailable.";
      return Navigation::Failed;
    }
    execution.keyringSelected = false;
    _VMESSAGE("Inventory category %u selected", category);
    return Navigation::Waiting;
  }

  // Keys live behind the Misc tab's synthetic Keyring row, not a sixth tab
  // button. Never choose a null-object grouping by its translated name.
  Tile *keyring = nullptr;
  for (auto *node = g_owner->itemList.GetHead(); node; node = node->GetNext()) {
    auto *row = node->GetItem();
    if (!row || row->byte08 || !ControlAvailable(row->tile) || (row->object && row->object->pObject))
      continue;
    if (keyring) {
      error = "Multiple inventory groupings are visible; the native keyring cannot be identified uniquely.";
      return Navigation::Failed;
    }
    keyring = row->tile;
  }
  if (!keyring) {
    error = "The native keyring row is unavailable in the Misc category.";
    return Navigation::Failed;
  }
  if (!execution.keyringSelected) {
    if (!Highlight(keyring)) {
      error = "The native keyring could not be selected.";
      return Navigation::Failed;
    }
    execution.keyringSelected = true;
  } else if (g_owner->itemList.GetSelectedTile() != keyring || !UIUtils::ClickTile(g_owner, keyring)) {
    error = "The selected keyring changed before it could be opened.";
    return Navigation::Failed;
  } else {
    execution.keyringSelected = false;
  }
  return Navigation::Waiting;
}

std::vector<StackState> ReadFormState(uint32_t formId) {
  std::vector<StackState> state;
  for (auto *node = g_owner->itemList.GetHead(); node; node = node->GetNext()) {
    auto *row = node->GetItem();
    auto *item = row ? row->object : nullptr;
    if (item && item->pObject && item->iNumber > 0 && item->pObject->GetFormID() == formId)
      state.push_back({Inventory::GetItemIdentity(*item), item->iNumber, item->GetWorn(false)});
  }
  return state;
}

int64_t TotalCount(const std::vector<StackState> &state) {
  int64_t count = 0;
  for (const auto &stack : state)
    count += stack.count;
  return count;
}

bool EquippedAfterSplit(const Execution &execution, const std::vector<StackState> &after) {
  if (execution.identity.extra)
    return false;
  // Equipping an ordinary stack can give one instance new extra data. Confirm
  // only a unique new worn instance, conservation of that stack, and unchanged
  // quantities/identities for every other pre-existing instance of this form.
  int newEquipped = 0;
  int targetCount = 0;
  for (const auto &stack : after) {
    if (stack.identity == execution.identity) {
      targetCount += stack.count;
      continue;
    }
    const auto previous = std::find_if(execution.before.begin(), execution.before.end(),
                                       [&](const StackState &entry) { return entry.identity == stack.identity; });
    if (previous == execution.before.end()) {
      if (!stack.identity.extra || !stack.equipped || stack.count > execution.count)
        return false;
      ++newEquipped;
      targetCount += stack.count;
    } else if (previous->count != stack.count || (!previous->equipped && stack.equipped)) {
      return false;
    }
  }
  for (const auto &stack : execution.before) {
    if (stack.identity == execution.identity)
      continue;
    if (std::none_of(after.begin(), after.end(),
                     [&](const StackState &entry) { return entry.identity == stack.identity; }))
      return false;
  }
  return newEquipped == 1 && targetCount == execution.count;
}

bool UnequippedAfterMerge(const Execution &execution, const std::vector<StackState> &after) {
  // A worn-only extra-data instance can merge back into the ordinary stack.
  // This is an observation after our own click, never a substitute selection.
  int merged = 0;
  for (const auto &stack : after) {
    const auto previous = std::find_if(execution.before.begin(), execution.before.end(),
                                       [&](const StackState &entry) { return entry.identity == stack.identity; });
    if (!stack.identity.extra && !stack.equipped &&
        stack.count ==
            static_cast<int64_t>(execution.count) + (previous == execution.before.end() ? 0 : previous->count)) {
      ++merged;
    } else if (previous == execution.before.end() || previous->count != stack.count ||
               previous->equipped != stack.equipped) {
      return false;
    }
  }
  for (const auto &stack : execution.before) {
    if (stack.identity == execution.identity)
      continue;
    if (std::none_of(after.begin(), after.end(),
                     [&](const StackState &entry) { return entry.identity == stack.identity; }))
      return false;
  }
  return merged == 1;
}

void FinishItem() {
  const auto &execution = g_queue.front();
  const char *verb = execution.operation == Operation::Equip     ? "Equipped "
                     : execution.operation == Operation::Unequip ? "Unequipped "
                     : execution.operation == Operation::Use     ? "Used "
                                                                 : "Dropped ";
  std::string context = "## Inventory\n" + std::string(verb) + execution.name;
  if (execution.operation == Operation::Drop)
    context += " x" + std::to_string(execution.count);
  else if (execution.operation == Operation::Use)
    context += " x1";
  context += ". Query your inventory again to refresh equipped states and reuse this stack's index.";
  NeuroSDK::SendContext(context.c_str(), true);
  _MESSAGE("Inventory action completed: %s%s", verb, execution.name.c_str());
  g_queue.pop_front();
}

bool ObserveItem(const Execution &execution) {
  bool ambiguous = false;
  auto *row = FindLive(execution.identity, &ambiguous);
  if (ambiguous) {
    StopExecution("The activated stack became ambiguous; completion cannot be confirmed.");
    return true;
  }
  const auto after = ReadFormState(execution.identity.formId);
  const auto count = TotalCount(after);
  const auto previousCount = TotalCount(execution.before);
  bool complete = false;
  switch (execution.operation) {
  case Operation::Equip:
    complete = count == previousCount && ((row && row->object->GetWorn(false)) || EquippedAfterSplit(execution, after));
    break;
  case Operation::Unequip:
    complete = count == previousCount &&
               ((row && !row->object->GetWorn(false)) || (!row && UnequippedAfterMerge(execution, after)));
    break;
  case Operation::Use:
    complete = count == previousCount - 1 &&
               (execution.count == 1 ? !row : row && row->object->iNumber == execution.count - 1);
    break;
  case Operation::Drop:
    complete = count == previousCount - execution.count && !row;
    break;
  }
  if (complete) {
    FinishItem();
    return true;
  }
  return false;
}

bool PublishPage(const Execution &execution) {
  auto *selected = execution.items.empty() ? nullptr : FindLive(execution.identity);
  if (g_owner->filter != execution.category ||
      (!execution.items.empty() && (!selected || selected->byte08 || !ControlAvailable(selected->tile) ||
                                    g_owner->itemList.GetSelectedTile() != selected->tile))) {
    StopExecution("The requested category or visible item selection changed before the page could be published.");
    return false;
  }
  const size_t first = static_cast<size_t>(execution.page) * Inventory::kItemsPerPage;
  const auto current = Inventory::ReadMenuItemPage(g_owner->itemList, execution.type, first);
  if (Inventory::CountMenuItems(g_owner->itemList, execution.type) != execution.total ||
      current.size() != execution.items.size() ||
      !std::equal(current.begin(), current.end(), execution.items.begin(), [](const auto &left, const auto &right) {
        return left.identity == right.identity && left.name == right.name && left.count == right.count &&
               left.equipped == right.equipped;
      })) {
    StopExecution("The inventory changed while the requested page was being shown.");
    return false;
  }
  std::ostringstream context;
  context << "## Your inventory: Pip-Boy\nType: " << Inventory::ItemQueryTypeName(execution.type) << "; page "
          << execution.page << "; " << execution.total << " item stacks. Pages start at 0; item indexes start at 1.\n";
  if (!execution.total)
    context << (execution.type == Inventory::ItemQueryType::All ? "Empty.\n" : "No items match this type.\n");
  for (size_t offset = 0; offset < execution.items.size(); ++offset) {
    const auto &item = execution.items[offset];
    context << "\n- `" << first + offset + 1 << "` - " << item.name << " x" << item.count;
    if (item.equipped)
      context << " (equipped)";
  }
  if (first + execution.items.size() < execution.total)
    context << "\nUse query_own_items with index " << execution.page + 1 << " and the same type for the next 15 items.";
  context << "\nItem actions use stable indexes from this listing. An accepted index cannot be reused until "
             "query_own_items refreshes the listing; other indexes stay fixed. Closing inventory or switching "
             "Pip-Boy tabs clears the listing.";
  if (execution.opening)
    context << "\nInventory actions: query_own_items, equip_item, unequip_item, use_item, drop_item, close_pipboy. "
               "Equip and unequip apply to weapons, armor and clothing; use_item consumes one aid item; drop_item "
               "drops the whole stack. Query types: All, Weapons, Equipment, Consumerable, WeaponMods, Keys, Misc; "
               "type defaults to All. Weapons includes ammunition; Consumerable includes food, drinks, chems and "
               "ingredients. Queries and closing wait for accepted item actions to finish.";
  if (!NeuroSDK::SendContext(context.str().c_str(), true))
    return false;
  g_listing.Replace(execution.items, first + 1);
  return true;
}

void AdvanceQuantity(Execution &execution, Clock::time_point now) {
  // This phase is reached only after our own native Drop activation.
  if (execution.query || execution.operation != Operation::Drop || execution.phase != Execution::Phase::Confirm)
    return;
  auto *manager = InterfaceManager::GetSingleton();
  auto *menu = manager ? manager->activeMenu : nullptr;
  if (!menu || menu->GetID() != Interface::Quantity)
    return;
  if (execution.quantityOwner && execution.quantityOwner != menu) {
    StopExecution("A different quantity prompt replaced the accepted drop prompt.");
    return;
  }
  auto *source = FindLive(execution.identity);
  if (!source || source->object->iNumber != execution.count) {
    // A completed one-item Drop can be followed by an unrelated quantity menu.
    // Observe the result without confirming or cancelling that other prompt.
    if (!ObserveItem(execution))
      StopExecution("The selected stack changed before its drop quantity could be confirmed.");
    return;
  }
  execution.quantityObserved = true;
  execution.quantityOwner = menu;
  auto *quantity = static_cast<QuantityMenu *>(menu);
  auto *maximum = quantity->tile28 ? quantity->tile28->GetValue(kTileValue_user0) : nullptr;
  if (!maximum || maximum->num != static_cast<float>(execution.count)) {
    UIUtils::ClickControl(quantity, "NOGLOW_BRANCH/QM_MainRect/QM_CancelButton");
    StopExecution("The quantity prompt no longer matches the selected whole stack.");
    return;
  }
  if (quantity->currentQtt < maximum->num) {
    if (!UIUtils::ClickControl(quantity, "NOGLOW_BRANCH/QM_MainRect/QM_IncreaseArrow")) {
      StopExecution("The native quantity control is unavailable.");
      return;
    }
    execution.nextStepAt = now + std::chrono::milliseconds(80);
    return;
  }
  if (quantity->currentQtt != maximum->num ||
      !UIUtils::ClickControl(quantity, "NOGLOW_BRANCH/QM_MainRect/QM_OKButton")) {
    StopExecution("The requested drop quantity could not be confirmed.");
    return;
  }
  execution.phase = Execution::Phase::Observe;
}

void AdvanceExecution() {
  if (g_queue.empty())
    return;
  auto &execution = g_queue.front();
  const auto now = Clock::now();
  if (execution.startedAt == Clock::time_point{}) {
    execution.startedAt = now;
    execution.nextStepAt = now + kStepDelay;
  }
  if (now - execution.startedAt > kExecutionTimeout) {
    if (Interface::GetTopMenuID() == Interface::Quantity && !execution.query &&
        execution.operation == Operation::Drop && execution.phase == Execution::Phase::Confirm &&
        execution.quantityObserved) {
      auto *manager = InterfaceManager::GetSingleton();
      auto *menu = manager ? manager->activeMenu : nullptr;
      auto *source = FindLive(execution.identity);
      if (menu && menu == execution.quantityOwner && menu->GetID() == Interface::Quantity && source &&
          source->object->iNumber == execution.count)
        UIUtils::ClickControl(menu, "NOGLOW_BRANCH/QM_MainRect/QM_CancelButton");
    }
    StopExecution("The visible action timed out or the game refused the selected item.");
    return;
  }
  if (now < execution.nextStepAt)
    return;
  const bool quantityActive = Interface::GetTopMenuID() == Interface::Quantity;
  if (!IsActive() && !quantityActive)
    return; // Pause for warnings, repairs and other menus without touching their controls.
  execution.nextStepAt = now + kStepDelay;
  if (quantityActive) {
    AdvanceQuantity(execution, now);
    return;
  }

  if (execution.phase == Execution::Phase::Read) {
    if (auto error = ValidatePage(execution.page, execution.type)) {
      StopExecution(*error);
      return;
    }
    const size_t first = static_cast<size_t>(execution.page) * Inventory::kItemsPerPage;
    execution.total = Inventory::CountMenuItems(g_owner->itemList, execution.type);
    execution.items = Inventory::ReadMenuItemPage(g_owner->itemList, execution.type, first);
    execution.category = EmptyQueryCategory(execution.type);
    if (!execution.items.empty()) {
      const auto &firstItem = execution.items.front();
      auto *row = FindLive(firstItem.identity);
      if (!row) {
        StopExecution("The first stack on the requested page cannot be identified uniquely.");
        return;
      }
      execution.identity = firstItem.identity;
      execution.count = firstItem.count;
      execution.category = NativeCategory(*row->object->pObject);
    }
    execution.phase = Execution::Phase::Navigate;
    return;
  }
  if (execution.phase == Execution::Phase::Observe || execution.phase == Execution::Phase::Confirm) {
    if (!ObserveItem(execution) && execution.phase == Execution::Phase::Confirm && execution.quantityObserved)
      StopExecution("The drop quantity prompt closed without completing the whole-stack drop.");
    return;
  }
  if (execution.phase == Execution::Phase::Publish) {
    if (PublishPage(execution))
      g_queue.pop_front();
    return;
  }
  if (execution.phase == Execution::Phase::Navigate) {
    std::string error;
    const auto navigation = Navigate(execution, error);
    if (navigation == Navigation::Failed) {
      StopExecution(error);
      return;
    }
    if (navigation == Navigation::Ready)
      execution.phase =
          execution.query && execution.items.empty() ? Execution::Phase::Publish : Execution::Phase::Select;
    return;
  }

  auto *row = FindLive(execution.identity);
  if (!row || row->object->iNumber != execution.count) {
    StopExecution("The selected stack disappeared, changed quantity or became ambiguous.");
    return;
  }
  if (g_owner->filter != execution.category || row->byte08) {
    StopExecution("The selected stack is hidden by a changed category, search or inventory grouping.");
    return;
  }
  if (!execution.query) {
    if (auto error = ValidateOperation(execution.operation, *row->object)) {
      StopExecution(*error);
      return;
    }
  }
  if (execution.phase == Execution::Phase::Select) {
    if (!Highlight(row->tile)) {
      StopExecution("The live item could not be visibly selected.");
      return;
    }
    _VMESSAGE("Inventory stack selected: %08X", execution.identity.formId);
    execution.phase = execution.query ? Execution::Phase::Publish : Execution::Phase::Activate;
    return;
  }
  if (g_owner->itemList.GetSelectedTile() != row->tile || !ControlAvailable(row->tile)) {
    StopExecution("The visible item selection changed before activation.");
    return;
  }
  // Explicit equip/unequip operations never blindly toggle an item. Another
  // queued equip may already have unequipped this one through native slot rules.
  if ((execution.operation == Operation::Equip && row->object->GetWorn(false)) ||
      (execution.operation == Operation::Unequip && !row->object->GetWorn(false))) {
    FinishItem();
    return;
  }
  execution.before = ReadFormState(execution.identity.formId);
  if (execution.operation == Operation::Drop) {
    auto *selection = InventoryMenu::Selection();
    if (!selection || !Inventory::MatchesItemIdentity(*selection, execution.identity) ||
        selection->iNumber != execution.count || !UIUtils::ClickTile(g_owner, g_owner->tile044)) {
      StopExecution("The native drop control or its selected stack is unavailable.");
      return;
    }
    // The XML makes this button controller-only on PC, but its native HandleClick
    // is the Drop shortcut. Visible selection still precedes this activation.
    execution.phase = Execution::Phase::Confirm;
  } else {
    auto *equippable = g_owner->tile ? g_owner->tile->GetValueName("_EquippableItem") : nullptr;
    if (!equippable || equippable->num == 0.0f || !UIUtils::ClickTile(g_owner, row->tile)) {
      StopExecution("The game does not allow equipping, unequipping or using the highlighted item.");
      return;
    }
    execution.phase = Execution::Phase::Observe;
  }
  _VMESSAGE("Inventory native item action activated: %08X", execution.identity.formId);
}

void BuildActions() {
  if (g_actionsBuilt)
    return;
  g_actions.Add(std::make_unique<Actions::Inventory::QueryOwnItemsAction>())
      .Add(std::make_unique<Actions::Inventory::EquipItemAction>())
      .Add(std::make_unique<Actions::Inventory::UnequipItemAction>())
      .Add(std::make_unique<Actions::Inventory::UseItemAction>())
      .Add(std::make_unique<Actions::Inventory::DropItemAction>());
  g_actionsBuilt = true;
}

} // namespace

bool IsExecuting() { return !g_queue.empty(); }

void Reset() {
  g_actions.Unregister();
  g_owner = nullptr;
  ++g_session;
  g_initialAttempted = false;
  g_listing.Clear();
  g_queue.clear();
}

bool Process() {
  auto *menu = InventoryMenu::GetSingleton();
  const auto tab = PipBoyHandler::CurrentTab();
  if (!menu || !Menu::IsMenuVisible(Interface::Inventory) || (tab && *tab != PipBoyHandler::Tab::Items)) {
    if (g_owner) {
      if (!g_queue.empty())
        StopExecution("The inventory closed or another Pip-Boy tab was opened before completion.");
      Reset();
    } else {
      g_actions.Unregister();
    }
    return false;
  }
  if (menu != g_owner) {
    if (!g_queue.empty())
      StopExecution("The inventory menu was replaced.");
    Reset();
    g_owner = menu;
    g_openedAt = Clock::now();
  }
  if (IsActive() && !g_initialAttempted && Clock::now() - g_openedAt >= kStepDelay) {
    g_initialAttempted = true;
    g_queue.push_back({.phase = Execution::Phase::Read, .query = true, .opening = true});
  }
  AdvanceExecution();
  if (!IsActive()) {
    g_actions.Unregister();
    return IsExecuting();
  }
  BuildActions();
  // A query owns navigation and replacement of the published snapshot. Item
  // actions keep a stable set registered so different listed indexes queue FIFO.
  if (g_initialAttempted && (g_queue.empty() || !g_queue.front().query))
    g_actions.Register();
  else
    g_actions.Unregister();
  return true;
}

Actions::PreparedAction PrepareQuery(int page, Inventory::ItemQueryType type) {
  const uint64_t session = g_session;
  auto revalidate = [session, page, type]() -> std::optional<std::string> {
    if (auto error = ValidateSession(session))
      return error;
    if (IsExecuting())
      return "Wait for the accepted inventory actions to finish before querying a new listing.";
    return ValidatePage(page, type);
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  return Actions::PreparedAction::Success(
      [page, type] { g_queue.push_back({.phase = Execution::Phase::Read, .query = true, .page = page, .type = type}); },
      revalidate);
}

Actions::PreparedAction PrepareItem(Operation operation, int index) {
  const uint64_t session = g_session;
  if (auto error = ValidateSession(session))
    return Actions::PreparedAction::Failure(*error);
  if (!g_listing.Available())
    return Actions::PreparedAction::Failure("Call query_own_items first to obtain current inventory indexes.");
  auto *item = g_listing.Find(index);
  if (!item)
    return Actions::PreparedAction::Failure("Index is not in your last inventory listing. Use query_own_items first.");
  const auto identity = item->identity;
  const int count = item->count;
  const uint64_t revision = g_listing.Revision();
  auto revalidate = [session, operation, index, identity, count, revision]() -> std::optional<std::string> {
    if (auto error = ValidateSession(session))
      return error;
    if (g_listing.Revision() != revision)
      return "The inventory listing changed. Use the indexes in the latest query_own_items listing.";
    const auto *item = g_listing.Find(index);
    if (!item || item->consumed)
      return "That listed stack already has an accepted action. Choose another index or query again after it finishes.";
    if (!g_queue.empty() && g_queue.front().query)
      return "Wait for the visible inventory query to finish before using its indexes.";
    auto *row = FindLive(identity);
    if (!row || row->object->iNumber != count)
      return "The stack changed or cannot be identified uniquely. Query your inventory again.";
    return ValidateOperation(operation, *row->object);
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  auto *row = FindLive(identity);
  const uint32_t category = NativeCategory(*row->object->pObject);
  const std::string name = item->name;
  return Actions::PreparedAction::Success(
      [operation, index, identity, count, category, name] {
        g_listing.Consume(index);
        g_queue.push_back(
            {.operation = operation, .identity = identity, .name = name, .count = count, .category = category});
      },
      revalidate);
}

} // namespace Menus::InventoryHandler
