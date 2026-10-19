#include "StubMenuHandler.hpp"
#include "Actions/Menu/CloseMenuAction.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/UIUtils.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace Menus::StubMenuHandler {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kStepDelay = std::chrono::milliseconds(350);
constexpr auto kCloseTimeout = std::chrono::seconds(5);

struct Entry {
  unsigned menuId;
  const char *action;
  const char *title;
  // Native exit control, relative to the menu root. Paths follow the private menu XML (extracts/menus) and the
  // names of each menu's Exit/Cancel/Done button; none of them has been clicked in game yet.
  const char *exitControl;
};

// Menus left out on purpose: LevelUp (must be completed, not skipped), Hacking, Computers, Surgery and Caravan (their
// exits are handled in code, with no XML control), BlackJack (its exit button doubles as Stay) and Dialog, which has a
// real handler.
constexpr std::array kEntries = {
    Entry{Interface::Barter, "close_barter", "barter", "NOGLOW_BRANCH/BM_ButtonRect/BM_ButtonB"},
    Entry{Interface::LockPick, "close_lockpick", "lockpicking",
          "NOGLOW_BRANCH/LPM_MainRect/LPM_ButtonRect/LPM_ExitButton"},
    Entry{Interface::VendorRepair, "close_repair_services", "repair services",
          "NOGLOW_BRANCH/RSM_MainRect/RSM_ButtonRect/RSM_DoneButton"},
    Entry{Interface::PipboyRepair, "close_repair", "item repair",
          "GLOW_BRANCH/RM_MainRect/RM_ButtonRect/RM_CancelButton"},
    Entry{Interface::ItemModMenu, "close_item_mod", "weapon modification",
          "GLOW_BRANCH/IMM_MainRect/IMM_ButtonRect/IMM_CancelButton"},
    Entry{Interface::Recipe, "close_recipe", "crafting recipe", "NOGLOW_BRANCH/RM_ButtonRect/RM_ButtonB"},
    Entry{Interface::SleepWait, "close_sleep_wait", "sleep and wait", "NOGLOW_BRANCH/SWM_MainRect/SWM_CancelButton"},
    Entry{Interface::SlotMachine, "close_slot_machine", "slot machine",
          "NOGLOW_BRANCH/SMM_MainRect/SMM_ButtonRect/SMM_ExitButton"},
    Entry{Interface::Roulette, "close_roulette", "roulette",
          "NOGLOW_BRANCH/ROM_MainRect/ROM_ButtonRect/ROM_ExitButton"},
    Entry{Interface::CompanionWheel, "close_companion_wheel", "companion wheel",
          "NOGLOW_BRANCH/CWM_MainRect/CWM_Button_Exit"},
    Entry{Interface::Tutorial, "close_tutorial", "tutorial",
          "NOGLOW_BRANCH/TM_Background/TM_depth_rect/TM_close_button"},
    Entry{Interface::BookMenu, "close_book", "book", "book_background/book_exit"},
};

// An accepted close: the native exit control is clicked after a visible step, then the menu must leave.
struct CloseRequest {
  Clock::time_point startedAt{};
  Clock::time_point nextStepAt{};
  bool clicked = false;
};

std::array<std::unique_ptr<Actions::PersistentActionSet>, kEntries.size()> g_sets;
const Entry *g_open = nullptr; // the stubbed menu currently open
bool g_actionable = false;     // it is the top menu, so it owns the controls
bool g_announced = false;
uint64_t g_session = 0;
std::optional<CloseRequest> g_close;

const Entry *FindEntry(unsigned menuId) {
  for (const auto &entry : kEntries)
    if (entry.menuId == menuId)
      return &entry;
  return nullptr;
}

Actions::PersistentActionSet &SetFor(const Entry &entry) {
  auto &slot = g_sets[&entry - kEntries.data()];
  if (!slot) {
    slot = std::make_unique<Actions::PersistentActionSet>();
    slot->Add(std::make_unique<Actions::Menu::CloseMenuAction>(entry.menuId, entry.action, entry.title));
  }
  return *slot;
}

void UnregisterAll() {
  for (auto &set : g_sets)
    if (set)
      set->Unregister();
}

const Entry *FindOpenEntry() {
  for (const auto &entry : kEntries)
    if (::Menu::IsMenuVisible(static_cast<Interface::Menus>(entry.menuId)))
      return &entry;
  return nullptr;
}

::Menu *MenuOf(const Entry &entry) { return InterfaceManager::GetMenuByType(entry.menuId); }

void FailClose(const Entry &entry, const std::string &reason) {
  _WARNING("Closing the %s menu failed: %s", entry.title, reason.c_str());
  g_close.reset();
  NeuroSDK::SendContext(("## " + std::string(entry.title) + " menu\n" + reason + " The menu is still open; try " +
                         entry.action + " again.")
                            .c_str());
}

void AdvanceClose(const Entry &entry) {
  if (!g_close || !g_actionable)
    return;
  const auto now = Clock::now();
  if (g_close->startedAt == Clock::time_point{}) {
    g_close->startedAt = now;
    g_close->nextStepAt = now + kStepDelay;
  }
  if (now - g_close->startedAt > kCloseTimeout) {
    FailClose(entry, "The menu did not close.");
    return;
  }
  if (g_close->clicked || now < g_close->nextStepAt)
    return;
  auto *menu = MenuOf(entry);
  auto *control = UIUtils::GetControl(menu, entry.exitControl);
  if (!UIUtils::IsControlAvailable(control) || !UIUtils::ClickTile(menu, control)) {
    FailClose(entry, "Its exit control could not be found or used.");
    return;
  }
  g_close->clicked = true;
  _VMESSAGE("Clicked the exit control of the %s menu", entry.title);
}

void Leave() {
  if (g_open && g_close && g_close->clicked)
    NeuroSDK::SendContext(
        ("## " + std::string(g_open->title) + " menu\nYou closed the menu. Gameplay controls are available again.")
            .c_str(),
        true);
  UnregisterAll();
  g_open = nullptr;
  g_actionable = false;
  g_announced = false;
  g_close.reset();
  ++g_session;
}

} // namespace

void Reset() {
  UnregisterAll();
  g_open = nullptr;
  g_actionable = false;
  g_announced = false;
  g_close.reset();
  ++g_session;
}

bool Process() {
  const Entry *open = FindOpenEntry();
  if (open != g_open) {
    Leave();
    g_open = open;
    if (g_open)
      _MESSAGE("Stubbed menu opened: %s", g_open->title);
  }
  if (!g_open)
    return false;

  g_actionable = Interface::GetTopMenuID() == g_open->menuId && MenuOf(*g_open) && MenuOf(*g_open)->tile;
  if (!g_actionable) {
    // A popup or another menu is on top and owns the controls.
    UnregisterAll();
    return true;
  }
  if (!g_announced) {
    const std::string title = g_open->title;
    g_announced =
        NeuroSDK::SendContext(("## The " + title + " menu is open\nThe controls for the " + title +
                               " menu are not implemented yet, so you cannot do anything else in it. The only thing "
                               "you can do is close it with " +
                               g_open->action + ".")
                                  .c_str());
  }
  if (g_announced)
    SetFor(*g_open).Register();
  AdvanceClose(*g_open);
  return true;
}

Actions::PreparedAction PrepareClose(unsigned menuId) {
  const uint64_t session = g_session;
  auto revalidate = [session, menuId]() -> std::optional<std::string> {
    if (session != g_session || !g_open || g_open->menuId != menuId || !g_actionable)
      return "That menu is no longer the active menu.";
    if (g_close)
      return "The menu is already being closed.";
    return std::nullopt;
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  return Actions::PreparedAction::Success([] { g_close = CloseRequest{}; }, revalidate);
}

} // namespace Menus::StubMenuHandler
