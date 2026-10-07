#include "PipBoyHandler.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "Actions/PipBoy/ClosePipBoyAction.hpp"
#include "GameUI.h"
#include "GameplayHandler.hpp"
#include "Menus/InventoryHandler.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include <memory>
#include <string>

namespace Menus::PipBoyHandler {
namespace {

constexpr auto kTransitionTimeout = std::chrono::seconds(15);

struct Transition {
  Tab tab = Tab::Items;
  bool closing = false;
  bool activated = false;
  Clock::time_point startedAt;
  Clock::time_point nextStepAt;
};

Actions::PersistentActionSet g_actions;
bool g_actionsBuilt = false;
bool g_observedOpen = false;
uint64_t g_session = 0;
std::optional<Transition> g_transition;

Interface::Menus MenuId(Tab tab) {
  switch (tab) {
  case Tab::Stats:
    return Interface::Stats;
  case Tab::Items:
    return Interface::Inventory;
  case Tab::Data:
    return Interface::PipboyData;
  }
  return Interface::NoMenu;
}

void StopTransition(const char *reason) {
  _WARNING("Pip-Boy navigation stopped: %s", reason);
  NeuroSDK::SendContext((std::string("## Pip-Boy navigation stopped\n") + reason).c_str());
  g_transition.reset();
}

void AdvanceTransition() {
  if (!g_transition)
    return;
  auto *manager = InterfaceManager::GetSingleton();
  const auto now = Clock::now();
  auto &transition = *g_transition;
  if (!manager || now - transition.startedAt > kTransitionTimeout) {
    StopTransition("The native Pip-Boy animation did not complete. Close any obstructing menu before retrying.");
    return;
  }
  if (now < transition.nextStepAt)
    return;
  transition.nextStepAt = now + kStepDelay;

  if (transition.closing) {
    if (transition.activated) {
      if (manager->pipBoyMode == 0 && !IsOpen()) {
        NeuroSDK::SendContext("## Pip-Boy\nClosed. Gameplay controls are available again.", true);
        _MESSAGE("Pip-Boy visibly closed");
        g_transition.reset();
      }
      return;
    }
    if (!IsActive(transition.tab))
      return; // Popups retain their own controls; never close through them.
    manager->ClosePipboy(nullptr);
  } else {
    if (transition.activated) {
      if (IsActive(transition.tab)) {
        _MESSAGE("Pip-Boy requested tab visibly opened");
        g_transition.reset();
      }
      return;
    }
    if (manager->pipBoyMode != 0 || manager->IsInMenuMode()) {
      StopTransition("Another menu opened before the Pip-Boy could be opened.");
      return;
    }
    // Vendored JG's native calls animate the Pip-Boy. Its pinned TogglePipBoy
    // command corroborates mode 0 (closed), mode 3 (ready), and nullptr callbacks.
    manager->OpenPipboy(nullptr, MenuId(transition.tab));
  }
  transition.activated = true;
  _VMESSAGE("Pip-Boy native %s activated", transition.closing ? "close" : "open");
}

} // namespace

std::optional<Tab> CurrentTab() {
  uint32_t top = Interface::GetTopMenuID();
  if (top == Interface::MainFour) {
    // JG's UI commands and JIP's GetTopVisibleMenuID corroborate that the
    // rendered Pip-Boy can report the shared MainFour root instead of a tab ID.
    if (Menu::IsMenuVisible(Interface::PipboyRepair) || Menu::IsMenuVisible(Interface::ItemModMenu))
      return std::nullopt;
    auto *manager = InterfaceManager::GetSingleton();
    if (manager && manager->activeMenu)
      top = manager->activeMenu->GetID();
    if (top == Interface::MainFour) {
      if (Menu::IsMenuVisible(Interface::Inventory))
        top = Interface::Inventory;
      else if (Menu::IsMenuVisible(Interface::Stats))
        top = Interface::Stats;
      else if (Menu::IsMenuVisible(Interface::PipboyData))
        top = Interface::PipboyData;
    }
  }
  switch (top) {
  case Interface::Stats:
    return Tab::Stats;
  case Interface::Inventory:
    return Tab::Items;
  case Interface::PipboyData:
    return Tab::Data;
  default:
    return std::nullopt;
  }
}

bool IsOpen() {
  return Menu::IsMenuVisible(Interface::Stats) || Menu::IsMenuVisible(Interface::Inventory) ||
         Menu::IsMenuVisible(Interface::PipboyData);
}

bool IsActive(Tab tab) {
  auto *manager = InterfaceManager::GetSingleton();
  return manager && manager->pipBoyMode == 3 && Menu::IsMenuVisible(MenuId(tab)) && CurrentTab() == tab;
}

bool IsTransitioning() { return g_transition.has_value(); }

bool IsExecuting() {
  // Include future tab executors here so navigation and gameplay share one lockout.
  return IsTransitioning() || InventoryHandler::IsExecuting();
}

void Reset() {
  InventoryHandler::Reset();
  g_actions.Unregister();
  g_transition.reset();
  g_observedOpen = false;
  ++g_session;
}

bool Process() {
  AdvanceTransition();
  const bool open = IsOpen();
  if (open != g_observedOpen) {
    g_observedOpen = open;
    ++g_session;
  }

  // Add future Stats/Data observation and executors here. Only the active tab
  // publishes actions; shared navigation waits for tab-specific execution.
  const bool inventoryBlocks = InventoryHandler::Process();
  if (!g_actionsBuilt) {
    g_actions.Add(std::make_unique<Actions::PipBoy::ClosePipBoyAction>());
    g_actionsBuilt = true;
  }
  const auto tab = CurrentTab();
  if (tab && IsActive(*tab) && !IsExecuting())
    g_actions.Register();
  else
    g_actions.Unregister();
  return open || IsTransitioning() || inventoryBlocks;
}

Actions::PreparedAction PrepareOpen(Tab tab) {
  const uint64_t session = g_session;
  auto revalidate = [session]() -> std::optional<std::string> {
    std::string error;
    if (!GameplayHandler::ValidateGameplayAction(error))
      return error;
    auto *manager = InterfaceManager::GetSingleton();
    if (session != g_session || !manager || manager->IsInMenuMode() || manager->pipBoyMode != 0 || IsOpen() ||
        IsTransitioning())
      return "Wait for the current menu or Pip-Boy animation to finish before opening the Pip-Boy.";
    return std::nullopt;
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  return Actions::PreparedAction::Success(
      [tab] {
        const auto now = Clock::now();
        g_transition = Transition{.tab = tab, .startedAt = now, .nextStepAt = now + kStepDelay};
      },
      revalidate);
}

Actions::PreparedAction PrepareClose() {
  const uint64_t session = g_session;
  const auto tab = CurrentTab();
  auto revalidate = [session, tab]() -> std::optional<std::string> {
    if (session != g_session || !tab || !IsActive(*tab))
      return "The Pip-Boy is no longer the active menu. Finish the current popup first.";
    if (IsExecuting())
      return "Wait for the accepted Pip-Boy actions to finish before closing it.";
    return std::nullopt;
  };
  if (auto error = revalidate())
    return Actions::PreparedAction::Failure(*error);
  return Actions::PreparedAction::Success(
      [tab] {
        const auto now = Clock::now();
        g_transition = Transition{.tab = *tab, .closing = true, .startedAt = now, .nextStepAt = now + kStepDelay};
      },
      revalidate);
}

} // namespace Menus::PipBoyHandler
