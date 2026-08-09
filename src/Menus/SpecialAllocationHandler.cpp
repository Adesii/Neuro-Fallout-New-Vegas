#include "SpecialAllocationHandler.hpp"
#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "Actions/Menu/SetSpecialAction.hpp"
#include "GameAPI.h"
#include "GameObjects.h"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "defs/LoveTesterMenu.hpp"
#include "utils/UIUtils.hpp"
#include <chrono>
#include <memory>
#include <numeric>
#include <string>

namespace Menus::SpecialAllocationHandler {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kValueChangeDelay = std::chrono::milliseconds(180);
constexpr auto kPageChangeDelay = std::chrono::milliseconds(1500);
constexpr auto kReviewDelay = std::chrono::milliseconds(1500);
constexpr auto kExecutionTimeout = std::chrono::seconds(90);
constexpr auto kRecoveryTimeout = std::chrono::seconds(10);
constexpr int kMaxSteps = 120;

enum class ExecutionPhase { Reduce, Rewind, Allocate };

struct ExecutionState {
  Menu *owner = nullptr;
  SpecialValues targets{};
  Clock::time_point startedAt;
  Clock::time_point nextStepAt;
  Clock::time_point recoveryStartedAt;
  int steps = 0;
  ExecutionPhase phase = ExecutionPhase::Reduce;
  bool reviewed = false;
  bool waitingForClose = false;
  bool aborting = false;
};

std::unique_ptr<Actions::ActionWindow> g_window;
std::unique_ptr<ExecutionState> g_execution;

int GetSpecialValue(int index) {
  auto *player = PlayerCharacter::GetSingleton();
  return player ? static_cast<int>(player->avOwner.GetBaseActorValueI(eActorVal_Strength + index)) : 0;
}

std::string BuildState(int totalPoints) {
  static constexpr const char *names[] = {"Strength",     "Perception", "Endurance", "Charisma",
                                          "Intelligence", "Agility",    "Luck"};
  std::string state = "Available SPECIAL total: " + std::to_string(totalPoints) + ". Current values:";
  for (int index = 0; index < 7; ++index)
    state += " " + std::string(names[index]) + " " + std::to_string(GetSpecialValue(index)) + (index == 6 ? "." : ",");
  return state;
}

bool IsTopMenu() {
  auto *interfaceManager = InterfaceManager::GetSingleton();
  auto *menu = LoveTester::GetMenu();
  return Interface::GetTopMenuID() == Interface::LoveTester ||
         (interfaceManager && interfaceManager->IsInMenuMode() && menu && interfaceManager->activeMenu == menu);
}

bool IsValidMenuData(const LoveTester::Data *data) {
  return data && data->currentPage >= 0 && data->currentPage <= 8 && data->totalPoints >= 7 && data->totalPoints <= 70;
}

bool IsInitialPage(int page) { return page == 0 || page == 1; }

void CloseWindow() {
  if (!g_window)
    return;
  if (g_window->End())
    g_window.reset();
}

void AbortExecution(const std::string &reason) {
  if (!g_execution || g_execution->aborting)
    return;
  _WARNING("SPECIAL allocation stopped: %s", reason.c_str());
  NeuroSDK::SendContext(("SPECIAL allocation stopped: " + reason).c_str());
  g_execution->aborting = true;
  g_execution->waitingForClose = false;
  g_execution->recoveryStartedAt = Clock::now();
  g_execution->nextStepAt = g_execution->recoveryStartedAt;
}

bool ValuesMatch(const SpecialValues &targets) {
  for (int index = 0; index < 7; ++index) {
    if (GetSpecialValue(index) != targets[index])
      return false;
  }
  return true;
}

void AdvanceExecution(bool unobstructed) {
  auto *menu = LoveTester::GetMenu();
  if (!g_execution)
    return;
  if (!menu) {
    g_execution.reset();
    return;
  }
  if (menu != g_execution->owner) {
    AbortExecution("The Vitals Tester menu changed unexpectedly.");
    return;
  }
  if (!unobstructed)
    return;
  if (!IsTopMenu()) {
    if (g_execution->waitingForClose) {
      _MESSAGE("SPECIAL visual execution completed");
    } else {
      _WARNING("SPECIAL execution stopped because the Vitals Tester closed unexpectedly");
    }
    g_execution.reset();
    return;
  }

  const auto now = Clock::now();
  auto *data = LoveTester::GetData();
  if (!data)
    return;
  if (g_execution->aborting) {
    if (data->currentPage <= 0) {
      g_execution.reset();
      return;
    }
    if (now - g_execution->recoveryStartedAt >= kRecoveryTimeout) {
      if (UIUtils::ClickControl(menu, "exit_menu")) {
        g_execution->aborting = false;
        g_execution->waitingForClose = true;
        g_execution->startedAt = now;
      } else {
        g_execution->nextStepAt = now + kPageChangeDelay;
      }
      return;
    }
    if (now >= g_execution->nextStepAt && UIUtils::ClickControl(menu, "previous_page")) {
      ++g_execution->steps;
      g_execution->nextStepAt = now + kPageChangeDelay;
    }
    return;
  }
  if (now - g_execution->startedAt > kExecutionTimeout || g_execution->steps >= kMaxSteps) {
    AbortExecution("The visual allocation sequence timed out.");
    return;
  }
  if (now < g_execution->nextStepAt)
    return;

  if (g_execution->waitingForClose)
    return;

  const int page = data->currentPage;
  bool clicked = false;
  auto delay = kValueChangeDelay;
  if (page == 0) {
    clicked = UIUtils::ClickControl(menu, "next_page");
    if (clicked)
      _VMESSAGE("SPECIAL execution advancing from cover to Strength");
    delay = kPageChangeDelay;
  } else if (page >= 1 && page <= 7) {
    const int index = page - 1;
    const int current = GetSpecialValue(index);
    if (g_execution->phase == ExecutionPhase::Reduce) {
      if (current > g_execution->targets[index]) {
        clicked = UIUtils::ClickControl(menu, "decrease_value");
        if (clicked)
          _VMESSAGE("SPECIAL reduction pass page %d decreasing %d toward %d", page, current,
                    g_execution->targets[index]);
      } else {
        clicked = UIUtils::ClickControl(menu, "next_page");
        if (clicked)
          _VMESSAGE("SPECIAL reduction pass page %d complete; advancing", page);
        delay = kPageChangeDelay;
      }
    } else if (g_execution->phase == ExecutionPhase::Rewind) {
      if (page > 1) {
        clicked = UIUtils::ClickControl(menu, "previous_page");
        if (clicked)
          _VMESSAGE("SPECIAL execution rewinding from page %d", page);
        delay = kPageChangeDelay;
      } else {
        g_execution->phase = ExecutionPhase::Allocate;
        g_execution->nextStepAt = now + kReviewDelay;
        _MESSAGE("SPECIAL reduction pass complete; starting allocation pass");
        return;
      }
    } else {
      if (current < g_execution->targets[index]) {
        clicked = UIUtils::ClickControl(menu, "increase_value");
        if (clicked)
          _VMESSAGE("SPECIAL allocation pass page %d increasing %d toward %d", page, current,
                    g_execution->targets[index]);
      } else if (current > g_execution->targets[index]) {
        clicked = UIUtils::ClickControl(menu, "decrease_value");
        if (clicked)
          _VMESSAGE("SPECIAL allocation pass page %d decreasing %d toward %d", page, current,
                    g_execution->targets[index]);
      } else {
        clicked = UIUtils::ClickControl(menu, "next_page");
        if (clicked)
          _VMESSAGE("SPECIAL allocation pass page %d complete at %d; advancing", page, current);
        delay = kPageChangeDelay;
      }
    }
  } else if (page == 8) {
    if (g_execution->phase == ExecutionPhase::Reduce) {
      g_execution->phase = ExecutionPhase::Rewind;
      clicked = UIUtils::ClickControl(menu, "previous_page");
      if (clicked)
        _MESSAGE("SPECIAL reduction pass complete; rewinding to Strength");
      delay = kPageChangeDelay;
    } else if (g_execution->phase == ExecutionPhase::Rewind) {
      clicked = UIUtils::ClickControl(menu, "previous_page");
      delay = kPageChangeDelay;
    } else {
      if (!ValuesMatch(g_execution->targets)) {
        AbortExecution("The displayed SPECIAL values no longer match the accepted allocation.");
        return;
      }
      if (!g_execution->reviewed) {
        _VMESSAGE("SPECIAL execution reached review page");
        g_execution->reviewed = true;
        g_execution->nextStepAt = now + kReviewDelay;
        return;
      }
      clicked = UIUtils::ClickControl(menu, "exit_menu");
      if (clicked)
        _VMESSAGE("SPECIAL execution confirmed allocation and requested menu exit");
      g_execution->waitingForClose = clicked;
      delay = kPageChangeDelay;
    }
  } else {
    AbortExecution("The Vitals Tester is on an unknown page.");
    return;
  }

  if (!clicked) {
    AbortExecution("A required Vitals Tester control was unavailable.");
    return;
  }
  ++g_execution->steps;
  g_execution->nextStepAt = now + delay;
}

} // namespace

bool ValidateAllocation(const SpecialValues &values, std::string &error) {
  auto *menu = LoveTester::GetMenu();
  auto *data = LoveTester::GetData();
  return IsValidMenuData(data) && RevalidateAllocation(values, menu, data->currentPage, data->totalPoints, error);
}

bool RevalidateAllocation(const SpecialValues &values, const void *expectedOwner, int expectedPage, int expectedTotal,
                          std::string &error) {
  auto *menu = LoveTester::GetMenu();
  auto *data = LoveTester::GetData();
  if (!menu || menu != expectedOwner || !IsValidMenuData(data) || data->currentPage != expectedPage ||
      !IsInitialPage(data->currentPage) || data->totalPoints != expectedTotal || !IsTopMenu() || g_execution) {
    error = "The Vitals Tester is no longer ready for a SPECIAL allocation.";
    return false;
  }

  const int selectedTotal = std::accumulate(values.begin(), values.end(), 0);
  if (selectedTotal != data->totalPoints) {
    error = "Selected SPECIAL total is " + std::to_string(selectedTotal) + ". Available total is " +
            std::to_string(data->totalPoints) + ". Adjust all seven values and try again.";
    return false;
  }
  return true;
}

void StartExecution(const SpecialValues &values) {
  auto *menu = LoveTester::GetMenu();
  auto *data = LoveTester::GetData();
  std::string error;
  if (!data || !RevalidateAllocation(values, menu, data->currentPage, data->totalPoints, error)) {
    NeuroSDK::SendContext(("SPECIAL allocation could not start: " + error).c_str());
    return;
  }
  g_execution = std::make_unique<ExecutionState>(
      ExecutionState{.owner = menu, .targets = values, .startedAt = Clock::now(), .nextStepAt = Clock::now()});
  _MESSAGE("SPECIAL visual execution started: S%d P%d E%d C%d I%d A%d L%d", values[0], values[1], values[2], values[3],
           values[4], values[5], values[6]);
}

bool IsExecuting() { return g_execution != nullptr; }

void Reset() {
  if (g_window) {
    g_window->Abandon();
    g_window.reset();
  }
  g_execution.reset();
}

bool Process(bool unobstructed) {
  auto *menu = LoveTester::GetMenu();
  auto *data = LoveTester::GetData();
  const bool menuOpen = menu && IsTopMenu();
  static bool invalidOverlayLogged = false;
  if (Interface::GetTopMenuID() == Interface::LoveTester && (!menu || !IsValidMenuData(data))) {
    if (!invalidOverlayLogged) {
      _WARNING("Vitals Tester overlay invalid (singleton: %p, lookup: %p, page: %d, total: %d)", menu,
               LoveTester::GetMenuByType(), data ? data->currentPage : -1, data ? data->totalPoints : -1);
      invalidOverlayLogged = true;
    }
    CloseWindow();
    return true;
  }
  if (IsValidMenuData(data))
    invalidOverlayLogged = false;

  static Menu *lastObservedMenu = nullptr;
  auto *observedMenu = menuOpen ? menu : nullptr;
  if (observedMenu != lastObservedMenu) {
    if (observedMenu) {
      _DMESSAGE("Vitals Tester detected (menu: %p, top: %u, page: %d, total: %d, unobstructed: %s)", menu,
                Interface::GetTopMenuID(), data ? data->currentPage : -1, data ? data->totalPoints : -1,
                unobstructed ? "true" : "false");
    } else if (lastObservedMenu) {
      _DMESSAGE("Vitals Tester closed");
    }
    lastObservedMenu = observedMenu;
  }
  if (g_execution) {
    AdvanceExecution(unobstructed);
    return g_execution != nullptr || menuOpen;
  }

  if (Actions::ActionRegistry::Get().HasPendingResult("set_special"))
    return true;

  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Ended)
    g_window.reset();
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Closing) {
    CloseWindow();
    return true;
  }

  if (!menuOpen || !data || !unobstructed || !IsInitialPage(data->currentPage)) {
    CloseWindow();
    return menuOpen;
  }
  if (g_window)
    return true;

  const std::string state = BuildState(data->totalPoints);
  const std::string query =
      "Choose all seven SPECIAL values from 1 to 10. Their total must equal " + std::to_string(data->totalPoints) + ".";
  g_window = std::make_unique<Actions::ActionWindow>();
  _MESSAGE("Opening set_special action window: %s", state.c_str());
  g_window->SetContext("Vitals Tester ready. " + state)
      .Add(std::make_unique<Actions::Menu::SetSpecialAction>())
      .SetForce(query, state, NeuroSDK::ActionPriority::High);
  if (!g_window->Register()) {
    if (g_window->GetState() != Actions::ActionWindow::State::Closing)
      g_window.reset();
    return true;
  }
  return true;
}

} // namespace Menus::SpecialAllocationHandler
