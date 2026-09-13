#include "DialogHandler.hpp"
#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "Actions/Menu/SelectDialogAction.hpp"
#include "GameTiles.h"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/UIUtils.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Menus::DialogHandler {
namespace {

using Clock = std::chrono::steady_clock;

// Runtime layout and XML names verified against JIP LN NVSE and menus/dialog/dialog_menu.xml.
constexpr auto kSelectionDelay = std::chrono::milliseconds(1500);
constexpr auto kExecutionTimeout = std::chrono::seconds(10);
constexpr size_t kMaxPendingSpeechLines = 64;

struct DialogOption {
  Tile *tile = nullptr;
  std::string text;
};

struct SpokenLine {
  std::string speaker;
  std::string text;
};

struct ExecutionState {
  SelectionSnapshot selection;
  std::string responseText;
  Clock::time_point startedAt;
  Clock::time_point clickAt;
  bool selected = false;
  bool clicked = false;
};

std::unique_ptr<Actions::ActionWindow> g_window;
std::unique_ptr<ExecutionState> g_execution;
std::vector<SpokenLine> g_pendingSpeech;
DialogMenu *g_observedMenu = nullptr;
std::string g_lastSpokenLine;
bool g_wasShowingText = false;

DialogMenu *GetMenu() { return *reinterpret_cast<DialogMenu **>(0x11D9510); }

bool IsTopMenu(DialogMenu *menu) {
  auto *interfaceManager = InterfaceManager::GetSingleton();
  return menu && (Interface::GetTopMenuID() == Interface::Dialog ||
                  (interfaceManager && interfaceManager->IsInMenuMode() && interfaceManager->activeMenu == menu));
}

bool IsDialogVisible(DialogMenu *menu) { return menu && Menu::IsMenuVisible(Interface::Dialog); }

bool IsShowingText(DialogMenu *menu) {
  if (!menu || !menu->tile)
    return true;
  auto *value = menu->tile->GetValueName("_ShowingText");
  return !value || value->num != 0.0f;
}

std::vector<DialogOption> GetOptions(DialogMenu *menu) {
  std::vector<DialogOption> options;
  if (!menu)
    return options;
  for (auto *entry = menu->topicList.GetHead(); entry; entry = entry->GetNext()) {
    auto *item = entry->GetItem();
    if (!item || !item->tile)
      continue;
    const std::string text = UIUtils::GetTileString(item->tile);
    if (!text.empty())
      options.push_back({item->tile, text});
  }
  return options;
}

std::string BuildSignature(DialogMenu *menu, const std::vector<DialogOption> &options) {
  std::string signature = UIUtils::GetTileString(menu ? menu->tile034 : nullptr) + "\n" +
                          UIUtils::GetTileString(menu ? menu->tile038 : nullptr);
  for (const auto &option : options)
    signature += "\n" + option.text;
  return signature;
}

std::string BuildSpokenDialog(const char *heading) {
  std::string context = "## " + std::string(heading);
  for (const auto &line : g_pendingSpeech) {
    context += "\n- ";
    if (!line.speaker.empty())
      context += line.speaker + ": ";
    context += "\"" + line.text + "\"";
  }
  return context;
}

std::string BuildState(DialogMenu *menu, const std::vector<DialogOption> &options) {
  std::string state = BuildSpokenDialog("Spoken dialog since the last choice");
  if (g_pendingSpeech.empty()) {
    const std::string speaker = UIUtils::GetTileString(menu ? menu->tile034 : nullptr);
    const std::string text = UIUtils::GetTileString(menu ? menu->tile038 : nullptr);
    if (!text.empty())
      state += "\n- " + (speaker.empty() ? std::string() : speaker + ": ") + "\"" + text + "\"";
  }
  state += "\n\n## Dialog options";
  for (size_t index = 0; index < options.size(); ++index)
    state += "\n- `" + std::to_string(index) + "` - " + options[index].text;
  return state;
}

std::string ValidOptionsMessage(const std::vector<DialogOption> &options) {
  if (options.empty())
    return "No dialog options are currently available.";
  std::string message = "Select a current dialog option:";
  for (size_t index = 0; index < options.size(); ++index)
    message += " " + std::to_string(index) + " (\"" + options[index].text + "\")";
  return message;
}

bool MatchesSelection(const SelectionSnapshot &snapshot, DialogMenu *menu, const std::vector<DialogOption> &options) {
  return menu && menu == snapshot.owner && IsTopMenu(menu) && !IsShowingText(menu) && snapshot.index >= 0 &&
         static_cast<size_t>(snapshot.index) < options.size() && options[snapshot.index].tile == snapshot.tile &&
         BuildSignature(menu, options) == snapshot.signature;
}

void CloseWindow() {
  if (g_window && g_window->End())
    g_window.reset();
}

void StopExecution(const std::string &reason) {
  _WARNING("Dialog selection stopped: %s", reason.c_str());
  NeuroSDK::SendContext(("Dialog selection stopped: " + reason).c_str());
  g_execution.reset();
}

void FlushPendingSpeech(const char *heading) {
  if (g_pendingSpeech.empty())
    return;
  const std::string context = BuildSpokenDialog(heading);
  if (NeuroSDK::SendContext(context.c_str(), true))
    g_pendingSpeech.clear();
}

void ReportConfirmedResponse(const ExecutionState &execution) {
  const std::string context = "## Confirmed dialog response\n- \"" + execution.responseText + "\"";
  if (!NeuroSDK::SendContext(context.c_str(), true))
    _WARNING("Could not report the confirmed dialog response.");
}

void AdvanceExecution() {
  if (!g_execution)
    return;
  auto *menu = GetMenu();
  if (!IsDialogVisible(menu)) {
    if (!g_execution->clicked) {
      StopExecution("The dialog menu closed before the selected response was activated.");
      return;
    }
    ReportConfirmedResponse(*g_execution);
    _MESSAGE("Dialog visual selection completed after the dialog menu closed");
    g_execution.reset();
    return;
  }
  if (menu != g_execution->selection.owner) {
    StopExecution("The dialog menu changed unexpectedly.");
    return;
  }
  if (!IsTopMenu(menu))
    return;

  const auto now = Clock::now();
  if (now - g_execution->startedAt > kExecutionTimeout) {
    StopExecution("The visual selection sequence timed out.");
    return;
  }
  if (g_execution->clicked) {
    const auto options = GetOptions(menu);
    if (IsShowingText(menu) || BuildSignature(menu, options) != g_execution->selection.signature) {
      ReportConfirmedResponse(*g_execution);
      _MESSAGE("Dialog visual selection completed");
      g_execution.reset();
    }
    return;
  }

  const auto options = GetOptions(menu);
  if (!MatchesSelection(g_execution->selection, menu, options)) {
    StopExecution("The dialog options changed before activation.");
    return;
  }
  if (!g_execution->selected) {
    menu->topicList.SetSelectedTile(g_execution->selection.tile);
    menu->topicList.ScrollToHighlight();
    if (menu->topicList.GetSelectedTile() != g_execution->selection.tile) {
      StopExecution("The chosen dialog option could not be visibly selected.");
      return;
    }
    g_execution->selected = true;
    g_execution->clickAt = now + kSelectionDelay;
    _VMESSAGE("Dialog option %d selected and highlighted", g_execution->selection.index);
    return;
  }
  if (now < g_execution->clickAt)
    return;
  if (menu->topicList.GetSelectedTile() != g_execution->selection.tile) {
    StopExecution("The chosen dialog option lost its visible selection before activation.");
    return;
  }
  if (!UIUtils::ClickTile(menu, g_execution->selection.tile)) {
    StopExecution("The chosen dialog option could not be activated.");
    return;
  }
  g_execution->clicked = true;
  _VMESSAGE("Dialog option %d activated after visible selection", g_execution->selection.index);
}

} // namespace

void Observe() {
  auto *menu = GetMenu();
  const bool visible = IsDialogVisible(menu);
  if (!visible) {
    FlushPendingSpeech("Dialog ended after these spoken lines");
    g_observedMenu = nullptr;
    g_lastSpokenLine.clear();
    g_wasShowingText = false;
    return;
  }

  if (menu != g_observedMenu) {
    g_observedMenu = menu;
    g_lastSpokenLine.clear();
    g_wasShowingText = false;
  }
  const bool showingText = IsShowingText(menu);
  if (showingText && !g_wasShowingText)
    g_lastSpokenLine.clear();
  if (showingText) {
    const std::string speaker = UIUtils::GetTileString(menu->tile034);
    const std::string text = UIUtils::GetTileString(menu->tile038);
    const std::string signature = speaker + "\n" + text;
    if (!text.empty() && signature != g_lastSpokenLine) {
      g_pendingSpeech.push_back({speaker, text});
      if (g_pendingSpeech.size() > kMaxPendingSpeechLines)
        g_pendingSpeech.erase(g_pendingSpeech.begin());
      g_lastSpokenLine = signature;
      _DMESSAGE("Buffered dialog line from '%s': %s", speaker.c_str(), text.c_str());
    }
  }
  g_wasShowingText = showingText;
}

bool ValidateSelection(int index, SelectionSnapshot &snapshot, std::string &error) {
  auto *menu = GetMenu();
  const auto options = GetOptions(menu);
  if (!menu || !IsTopMenu(menu) || IsShowingText(menu) || g_execution) {
    error = "The dialog options are no longer ready for selection.";
    return false;
  }
  if (index < 0 || static_cast<size_t>(index) >= options.size()) {
    error = ValidOptionsMessage(options);
    return false;
  }
  snapshot = {.owner = menu, .tile = options[index].tile, .index = index, .signature = BuildSignature(menu, options)};
  return true;
}

bool RevalidateSelection(const SelectionSnapshot &snapshot, std::string &error) {
  auto *menu = GetMenu();
  const auto options = GetOptions(menu);
  if (g_execution || !MatchesSelection(snapshot, menu, options)) {
    error = "The dialog options changed. " + ValidOptionsMessage(options);
    return false;
  }
  return true;
}

void StartExecution(const SelectionSnapshot &snapshot) {
  std::string error;
  if (!RevalidateSelection(snapshot, error)) {
    NeuroSDK::SendContext(("Dialog selection could not start: " + error).c_str());
    return;
  }
  const auto now = Clock::now();
  g_execution = std::make_unique<ExecutionState>(ExecutionState{.selection = snapshot,
                                                                .responseText = UIUtils::GetTileString(snapshot.tile),
                                                                .startedAt = now,
                                                                .clickAt = now + kSelectionDelay});
}

void Reset() {
  if (g_window) {
    g_window->Abandon();
    g_window.reset();
  }
  g_execution.reset();
  g_pendingSpeech.clear();
  g_observedMenu = nullptr;
  g_lastSpokenLine.clear();
  g_wasShowingText = false;
}

bool Process(bool automationAllowed) {
  auto *menu = GetMenu();
  const bool menuOpen = IsTopMenu(menu);
  if (g_execution) {
    if (automationAllowed)
      AdvanceExecution();
    return g_execution != nullptr || menuOpen;
  }
  if (Actions::ActionRegistry::Get().HasPendingResult("select_dialog"))
    return true;

  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Ended)
    g_window.reset();
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Closing) {
    CloseWindow();
    return true;
  }

  const auto options = GetOptions(menu);
  if (!menuOpen || !automationAllowed || IsShowingText(menu) || options.empty()) {
    CloseWindow();
    return menuOpen;
  }
  FlushPendingSpeech("NPC dialog");

  const std::string signature = BuildSignature(menu, options);
  static std::string publishedSignature;
  if (g_window && signature == publishedSignature)
    return true;
  CloseWindow();
  if (g_window)
    return true;

  const std::string state = BuildState(menu, options);
  g_window = std::make_unique<Actions::ActionWindow>();
  g_window->Add(std::make_unique<Actions::Menu::SelectDialogAction>(options.size()))
      .SetForce("Choose the dialog response to say by its index.", state, NeuroSDK::ActionPriority::Medium);
  if (!g_window->Register()) {
    if (g_window->GetState() != Actions::ActionWindow::State::Closing)
      g_window.reset();
    return true;
  }
  publishedSignature = signature;
  _MESSAGE("Opened select_dialog action window with %zu option(s)", options.size());
  return true;
}

} // namespace Menus::DialogHandler
