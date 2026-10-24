#include "MessagePopupHandler.hpp"
#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "Actions/Menu/SelectPopupOptionAction.hpp"
#include "GameUI.h"
#include "Menus/Pacing.hpp"
#include "NeuroSDK.hpp"
#include "PopupPolicy.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Menus::MessagePopupHandler {
namespace {

using Pacing::Clock;
using Pacing::kRegisterRetryDelay;
using Pacing::kSelectionDelay;

constexpr auto kAcceptDelay = std::chrono::milliseconds(1500);
constexpr auto kExecutionTimeout = std::chrono::seconds(10);

struct PopupButton {
  Tile *tile = nullptr;
  std::string label;
};

struct ExecutionState {
  SelectionSnapshot selection;
  std::string label;
  ScopedState::StepTimer timer; // counts only while the popup is the top menu
  Clock::time_point clickAt;
  bool selected = false;
  bool clicked = false;
};

std::unique_ptr<Actions::ActionWindow> g_window;
std::unique_ptr<ExecutionState> g_execution;
std::string g_publishedSignature;
ScopedState::Backoff g_registerRetry{kRegisterRetryDelay};
ScopedState::Delay g_acceptDelay;
ScopedState::Observation g_context;

MessageMenu *GetOpenMenu() {
  auto *menu = MessageMenu::Get();
  if (!menu || !menu->tile || StartMenu::Get() || !UIUtils::IsTopMenu(menu, Interface::Message))
    return nullptr;
  return menu;
}

// The popup is still in the menu stack but something else (another menu or popup) is on top of it. Different from
// gone: nothing may be concluded about a covered popup, and its timers must not run. A visibility flag that lingers
// with no menu on top at all would otherwise block gameplay forever, so that case counts as gone.
bool IsCovered() { return Menu::IsMenuVisible(Interface::Message) && Interface::GetTopMenuID() != Interface::NoMenu; }

bool HasButton(MessageMenu *menu, Tile *tile) {
  for (auto *entry = menu->buttonList.GetHead(); entry; entry = entry->GetNext()) {
    auto *item = entry->GetItem();
    if (item && item->tile == tile)
      return true;
  }
  return false;
}

std::vector<PopupButton> GetButtons(MessageMenu *menu) {
  std::vector<PopupButton> buttons;
  for (auto *entry = menu->buttonList.GetHead(); entry; entry = entry->GetNext()) {
    auto *item = entry->GetItem();
    if (!item || !item->tile)
      continue;
    buttons.push_back({item->tile, UIUtils::GetTileString(item->tile)});
  }
  return buttons;
}

std::string BuildSignature(const std::string &title, const std::string &text, const std::vector<PopupButton> &buttons) {
  std::string signature = title + "\n" + text;
  for (const auto &button : buttons)
    signature += "\n" + button.label;
  return signature;
}

std::string BuildDescription(const std::string &title, const std::string &text,
                             const std::vector<PopupButton> &buttons) {
  std::string description = "## Message popup\n";
  if (!title.empty())
    description += "**" + title + "**\n\n";
  description += text;
  if (!buttons.empty()) {
    description += "\n\n## Options";
    for (size_t index = 0; index < buttons.size(); ++index)
      description += "\n- `" + std::to_string(index) + "` - " + buttons[index].label;
  }
  return description;
}

// The button the plugin presses on its own (see PopupPolicy), or null when Neuro decides.
Tile *FindAutomaticButton(const std::string &title, const std::string &text, const std::vector<PopupButton> &buttons) {
  std::vector<std::string> labels;
  labels.reserve(buttons.size());
  for (const auto &button : buttons)
    labels.push_back(button.label);
  const auto index = PopupPolicy::ChooseAutomaticButton(title, text, labels);
  return index ? buttons[*index].tile : nullptr;
}

std::string ValidOptionsMessage(const std::vector<PopupButton> &buttons) {
  if (buttons.empty())
    return "The popup has no buttons.";
  std::string message = "Choose a current popup button:";
  for (size_t index = 0; index < buttons.size(); ++index)
    message += " " + std::to_string(index) + " (\"" + buttons[index].label + "\")";
  return message;
}

bool MatchesSelection(const SelectionSnapshot &snapshot, MessageMenu *menu, const std::vector<PopupButton> &buttons) {
  return menu && menu == snapshot.owner && snapshot.index >= 0 &&
         static_cast<size_t>(snapshot.index) < buttons.size() && buttons[snapshot.index].tile == snapshot.tile &&
         BuildSignature(UIUtils::GetTileString(menu->titleTile), UIUtils::GetTileString(menu->messageText), buttons) ==
             snapshot.signature;
}

void CloseWindow() {
  if (g_window && g_window->End())
    g_window.reset();
}

void StopExecution(const std::string &reason) {
  _WARNING("Popup selection stopped: %s", reason.c_str());
  NeuroSDK::SendContext(("Popup selection stopped: " + reason).c_str());
  g_execution.reset();
}

void ReportChosen(const ExecutionState &execution) {
  const std::string context = "## Message popup\nChose \"" + execution.label + "\".";
  NeuroSDK::SendContext(context.c_str(), true);
}

void AdvanceExecution(MessageMenu *menu) {
  if (!g_execution)
    return;
  if (!menu) {
    if (IsCovered())
      return; // paused: the popup comes back to the top once whatever covers it is gone
    if (g_execution->clicked) {
      ReportChosen(*g_execution);
      _MESSAGE("Popup visual selection completed");
      g_execution.reset();
    } else {
      StopExecution("The popup closed before the chosen button was activated.");
    }
    return;
  }
  if (menu != g_execution->selection.owner) {
    StopExecution("The popup changed unexpectedly.");
    return;
  }
  const auto now = Clock::now();
  if (g_execution->timer.Expired(kExecutionTimeout)) {
    StopExecution("The visual popup selection timed out.");
    return;
  }

  const auto buttons = GetButtons(menu);
  const std::string signature =
      BuildSignature(UIUtils::GetTileString(menu->titleTile), UIUtils::GetTileString(menu->messageText), buttons);
  if (g_execution->clicked) {
    // The click was accepted when the popup changed, or when the clicked button is gone: an identical popup that
    // follows the chosen one has the same text but is built from new buttons.
    if (signature != g_execution->selection.signature || !HasButton(menu, g_execution->selection.tile)) {
      ReportChosen(*g_execution);
      _MESSAGE("Popup visual selection completed; another popup followed");
      g_execution.reset();
    }
    return;
  }
  if (!MatchesSelection(g_execution->selection, menu, buttons)) {
    StopExecution("The popup buttons changed before activation.");
    return;
  }
  if (!g_execution->selected) {
    menu->buttonList.SetSelectedTile(g_execution->selection.tile);
    menu->buttonList.ScrollToHighlight();
    if (menu->buttonList.GetSelectedTile() != g_execution->selection.tile) {
      StopExecution("The chosen popup button could not be visibly selected.");
      return;
    }
    g_execution->selected = true;
    g_execution->clickAt = now + kSelectionDelay;
    _VMESSAGE("Popup button %d selected and highlighted", g_execution->selection.index);
    return;
  }
  if (now < g_execution->clickAt)
    return;
  if (menu->buttonList.GetSelectedTile() != g_execution->selection.tile) {
    StopExecution("The chosen popup button lost its visible selection before activation.");
    return;
  }
  if (!UIUtils::ClickTile(menu, g_execution->selection.tile)) {
    StopExecution("The chosen popup button could not be activated.");
    return;
  }
  g_execution->clicked = true;
  _VMESSAGE("Popup button %d activated after visible selection", g_execution->selection.index);
}

} // namespace

void Reset() {
  if (g_window) {
    g_window->Release();
    g_window.reset();
  }
  g_execution.reset();
  g_publishedSignature.clear();
  g_registerRetry.Reset();
  g_acceptDelay.Reset();
  g_context.Reset();
}

bool ValidateSelection(int index, SelectionSnapshot &snapshot, std::string &error) {
  auto *menu = GetOpenMenu();
  if (!menu || g_execution) {
    error = "The message popup is no longer ready for selection.";
    return false;
  }
  const auto buttons = GetButtons(menu);
  if (index < 0 || static_cast<size_t>(index) >= buttons.size()) {
    error = ValidOptionsMessage(buttons);
    return false;
  }
  snapshot = {.owner = menu,
              .tile = buttons[index].tile,
              .index = index,
              .signature = BuildSignature(UIUtils::GetTileString(menu->titleTile),
                                          UIUtils::GetTileString(menu->messageText), buttons)};
  return true;
}

bool RevalidateSelection(const SelectionSnapshot &snapshot, std::string &error) {
  auto *menu = GetOpenMenu();
  const auto buttons = menu ? GetButtons(menu) : std::vector<PopupButton>{};
  if (g_execution || !MatchesSelection(snapshot, menu, buttons)) {
    error = "The popup changed. " + ValidOptionsMessage(buttons);
    return false;
  }
  return true;
}

void StartExecution(const SelectionSnapshot &snapshot) {
  std::string error;
  if (!RevalidateSelection(snapshot, error)) {
    NeuroSDK::SendContext(("Popup selection could not start: " + error).c_str());
    return;
  }
  const auto now = Clock::now();
  g_execution = std::make_unique<ExecutionState>(ExecutionState{
      .selection = snapshot, .label = UIUtils::GetTileString(snapshot.tile), .clickAt = now + kSelectionDelay});
  g_execution->timer.Start(now);
}

bool Process() {
  auto *menu = GetOpenMenu();
  if (g_execution) {
    g_execution->timer.Tick(menu != nullptr);
    AdvanceExecution(menu);
    return g_execution != nullptr || menu != nullptr;
  }
  if (!menu) {
    Reset();
    return false;
  }

  const std::string title = UIUtils::GetTileString(menu->titleTile);
  const std::string text = UIUtils::GetTileString(menu->messageText);
  const auto buttons = GetButtons(menu);
  const std::string signature = BuildSignature(title, text, buttons);

  if (!g_context.IsCurrent(menu, signature)) {
    // Observed popups are logged so the known-popup table can be extended from real play.
    // The text alone does not require a reaction: choosing a button is prompted by the forced action below.
    if (!NeuroSDK::SendContext(BuildDescription(title, text, buttons).c_str(), true))
      return true;
    g_context.Commit(menu, signature);
    std::string labels;
    for (const auto &button : buttons)
      labels += (labels.empty() ? "\"" : ", \"") + button.label + "\"";
    _MESSAGE("Message popup observed: title=\"%s\" text=\"%s\" buttons=[%s]", title.c_str(), text.c_str(),
             labels.c_str());
  }

  if (Actions::ActionRegistry::Get().HasPendingResult("choose_popup_option"))
    return true;
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Ended)
    g_window.reset();
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Closing) {
    CloseWindow();
    return true;
  }

  if (buttons.empty()) {
    CloseWindow();
    return true;
  }
  if (auto *known = FindAutomaticButton(title, text, buttons)) {
    CloseWindow();
    menu->buttonList.SetSelectedTile(known);
    if (g_acceptDelay.Ready(menu, signature, kAcceptDelay) && UIUtils::ClickTile(menu, known))
      g_acceptDelay.Reset();
    return true;
  }
  g_acceptDelay.Reset();

  if (g_window && signature == g_publishedSignature)
    return true;
  CloseWindow();
  if (g_window || !g_registerRetry.Ready())
    return true;

  g_window = std::make_unique<Actions::ActionWindow>();
  g_window->Add(std::make_unique<Actions::Menu::SelectPopupOptionAction>(buttons.size()))
      .SetForce("Choose how to respond to the message popup by its button index.",
                BuildDescription(title, text, buttons), NeuroSDK::ActionPriority::Medium);
  if (!g_window->Register()) {
    g_registerRetry.Fail();
    if (g_window->GetState() != Actions::ActionWindow::State::Closing)
      g_window.reset();
    return true;
  }
  g_publishedSignature = signature;
  _MESSAGE("Opened choose_popup_option action window with %zu button(s)", buttons.size());
  return true;
}

} // namespace Menus::MessagePopupHandler
