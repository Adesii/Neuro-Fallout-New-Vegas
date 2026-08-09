#include "TraitsHandler.hpp"
#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "Actions/Menu/DoneTraitsMenuAction.hpp"
#include "Actions/Menu/SelectTraitAction.hpp"
#include "GameForms.h"
#include "GameTiles.h"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/UIUtils.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Menus::TraitsHandler {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kVisualDelay = std::chrono::milliseconds(1200);
constexpr auto kVerifyDelay = std::chrono::milliseconds(400);
constexpr auto kExecutionTimeout = std::chrono::seconds(15);

struct TraitOption {
  Tile *tile = nullptr;
  std::string name;
  std::string description;
  bool selected = false;
};

enum class Phase {
  PrepareUnselect,
  ClickUnselect,
  VerifyUnselect,
  PrepareSelect,
  ClickSelect,
  VerifySelect,
  ClickDone,
  WaitClose
};

struct ExecutionState {
  SelectionSnapshot selection;
  DoneSnapshot done;
  Clock::time_point startedAt;
  Clock::time_point nextStepAt;
  Phase phase = Phase::PrepareSelect;
  bool finishing = false;
};

std::unique_ptr<Actions::ActionWindow> g_window;
std::unique_ptr<ExecutionState> g_execution;

TraitMenu *GetMenu() { return *reinterpret_cast<TraitMenu **>(0x11DAF74); }

bool IsTopMenu(TraitMenu *menu) {
  auto *interfaceManager = InterfaceManager::GetSingleton();
  return menu && (Interface::GetTopMenuID() == Interface::Traits ||
                  (interfaceManager && interfaceManager->IsInMenuMode() && interfaceManager->activeMenu == menu));
}

bool IsSelected(Tile *tile) {
  auto *value = tile ? tile->GetValueName("_selected") : nullptr;
  return value && value->num != 0.0f;
}

BGSPerk *FindPerk(TraitMenu *menu, const std::string &name) {
  if (!menu)
    return nullptr;
  for (tList<BGSPerk>::Iterator iter(menu->perkList); !iter.End(); ++iter) {
    auto *perk = iter.Get();
    const char *fullName = perk ? perk->fullName.GetFullName() : nullptr;
    if (fullName && name == fullName)
      return perk;
  }
  return nullptr;
}

std::vector<TraitOption> GetOptions(TraitMenu *menu, bool includeDescriptions = false) {
  std::vector<TraitOption> options;
  if (!menu)
    return options;
  for (auto *entry = menu->perkListBox.GetHead(); entry; entry = entry->GetNext()) {
    auto *item = entry->GetItem();
    if (!item || !item->tile)
      continue;
    std::string name = UIUtils::GetTileString(item->tile);
    if (name.empty())
      continue;
    std::string description;
    if (includeDescriptions) {
      auto *perk = FindPerk(menu, name);
      const char *text = perk ? perk->description.Get(perk, 'CSED') : nullptr;
      if (text)
        description = text;
    }
    options.push_back(
        {.tile = item->tile, .name = name, .description = description, .selected = IsSelected(item->tile)});
  }
  return options;
}

std::string BuildSignature(TraitMenu *menu, const std::vector<TraitOption> &options) {
  std::string signature =
      std::to_string(menu ? menu->numSelected : 0) + "/" + std::to_string(menu ? menu->maxSelect : 0);
  for (const auto &option : options)
    signature += "\n" + option.name + (option.selected ? ":1" : ":0");
  return signature;
}

std::string BuildState(TraitMenu *menu, const std::vector<TraitOption> &options) {
  const bool canSelect = menu->maxSelect > 0 && menu->numSelected <= menu->maxSelect;
  std::string state = "Selected trait slots: " + std::to_string(menu->numSelected) + " of up to " +
                      std::to_string(menu->maxSelect) + ".";
  state += " Traits are optional. You may choose done_traits_menu now, including with zero selected traits, to make "
           "no further changes.";
  if (canSelect)
    state += " select_trait requires select and unselect indexes. unselect is ignored while a free slot exists or no "
             "trait is selected, but it must still be a valid trait index.";
  else
    state += " Trait selection is unavailable in the current menu state.";
  state += "\nTraits:";
  for (size_t index = 0; index < options.size(); ++index) {
    state += "\n" + std::to_string(index) + (options[index].selected ? " [selected] " : " [not selected] ") +
             options[index].name + ": " + options[index].description;
  }
  return state;
}

std::string ValidOptionsMessage(const std::vector<TraitOption> &options) {
  std::string message = "Current traits:";
  for (size_t index = 0; index < options.size(); ++index)
    message += " " + std::to_string(index) + " (" + options[index].name +
               (options[index].selected ? ", selected)" : ", not selected)");
  return message;
}

bool HasTile(const std::vector<TraitOption> &options, Tile *tile) {
  return std::any_of(options.begin(), options.end(), [tile](const TraitOption &option) { return option.tile == tile; });
}

bool MatchesSelection(const SelectionSnapshot &snapshot, TraitMenu *menu, const std::vector<TraitOption> &options) {
  return menu && menu == snapshot.owner && IsTopMenu(menu) && snapshot.selectIndex >= 0 &&
         static_cast<size_t>(snapshot.selectIndex) < options.size() &&
         options[snapshot.selectIndex].tile == snapshot.selectTile &&
         (!snapshot.shouldUnselect ||
          (snapshot.unselectIndex >= 0 && static_cast<size_t>(snapshot.unselectIndex) < options.size() &&
           options[snapshot.unselectIndex].tile == snapshot.unselectTile)) &&
         BuildSignature(menu, options) == snapshot.signature;
}

bool MatchesDone(const DoneSnapshot &snapshot, TraitMenu *menu, const std::vector<TraitOption> &options) {
  return menu && menu == snapshot.owner && IsTopMenu(menu) && BuildSignature(menu, options) == snapshot.signature;
}

void CloseWindow() {
  if (g_window && g_window->End())
    g_window.reset();
}

void StopExecution(const std::string &reason) {
  _WARNING("Trait execution stopped: %s", reason.c_str());
  NeuroSDK::SendContext(("Trait selection stopped: " + reason).c_str());
  g_execution.reset();
}

bool SelectVisibly(TraitMenu *menu, Tile *tile) {
  menu->perkListBox.SetSelectedTile(tile);
  menu->perkListBox.ScrollToHighlight();
  return menu->perkListBox.GetSelectedTile() == tile;
}

void AdvanceExecution() {
  if (!g_execution)
    return;
  auto *menu = GetMenu();
  if (!menu || !Menu::IsMenuVisible(Interface::Traits)) {
    if (g_execution->finishing)
      _MESSAGE("Traits menu visual execution completed");
    else
      _WARNING("Traits menu closed during visual execution");
    g_execution.reset();
    return;
  }
  if (!IsTopMenu(menu))
    return;
  if (menu != (g_execution->finishing ? g_execution->done.owner : g_execution->selection.owner)) {
    StopExecution("The traits menu changed unexpectedly.");
    return;
  }
  const auto now = Clock::now();
  if (now - g_execution->startedAt > kExecutionTimeout) {
    StopExecution("The visual trait selection timed out.");
    return;
  }
  if (now < g_execution->nextStepAt)
    return;

  auto &execution = *g_execution;
  const auto options = GetOptions(menu);
  switch (execution.phase) {
  case Phase::PrepareUnselect:
    if (!MatchesSelection(execution.selection, menu, options) ||
        !SelectVisibly(menu, execution.selection.unselectTile)) {
      StopExecution("The trait to unselect is no longer available.");
      return;
    }
    execution.phase = Phase::ClickUnselect;
    execution.nextStepAt = now + kVisualDelay;
    _VMESSAGE("Visibly selected trait index %d for removal", execution.selection.unselectIndex);
    return;
  case Phase::ClickUnselect:
    if (menu->perkListBox.GetSelectedTile() != execution.selection.unselectTile ||
        !UIUtils::ClickTile(menu, execution.selection.unselectTile)) {
      StopExecution("The selected trait could not be removed.");
      return;
    }
    execution.phase = Phase::VerifyUnselect;
    execution.nextStepAt = now + kVerifyDelay;
    return;
  case Phase::VerifyUnselect:
    if (IsSelected(execution.selection.unselectTile)) {
      StopExecution("The selected trait remained active after its click.");
      return;
    }
    execution.phase = Phase::PrepareSelect;
    execution.nextStepAt = now + kVisualDelay;
    return;
  case Phase::PrepareSelect:
    if ((!execution.selection.shouldUnselect && !MatchesSelection(execution.selection, menu, options)) ||
        !HasTile(options, execution.selection.selectTile) || IsSelected(execution.selection.selectTile) ||
        !SelectVisibly(menu, execution.selection.selectTile)) {
      StopExecution("The new trait is no longer available for selection.");
      return;
    }
    execution.phase = Phase::ClickSelect;
    execution.nextStepAt = now + kVisualDelay;
    _VMESSAGE("Visibly selected trait index %d", execution.selection.selectIndex);
    return;
  case Phase::ClickSelect:
    if (menu->perkListBox.GetSelectedTile() != execution.selection.selectTile ||
        !UIUtils::ClickTile(menu, execution.selection.selectTile)) {
      StopExecution("The new trait could not be selected.");
      return;
    }
    execution.phase = Phase::VerifySelect;
    execution.nextStepAt = now + kVerifyDelay;
    return;
  case Phase::VerifySelect:
    if (!IsSelected(execution.selection.selectTile)) {
      StopExecution("The new trait was not active after its click.");
      return;
    }
    _MESSAGE("Trait visual selection completed");
    g_execution.reset();
    return;
  case Phase::ClickDone:
    if (!MatchesDone(execution.done, menu, options) || !UIUtils::ClickTile(menu, menu->tile40)) {
      StopExecution("The traits Done button could not be activated.");
      return;
    }
    execution.phase = Phase::WaitClose;
    execution.nextStepAt = now + kVisualDelay;
    _VMESSAGE("Traits Done button activated");
    return;
  case Phase::WaitClose:
    return;
  }
}

} // namespace

bool ValidateSelection(int select, int unselect, SelectionSnapshot &snapshot, std::string &error) {
  auto *menu = GetMenu();
  if (!menu || !IsTopMenu(menu) || g_execution) {
    error = "The traits menu is no longer ready.";
    return false;
  }
  if (menu->maxSelect == 0 || menu->numSelected > menu->maxSelect) {
    error = "The traits menu selection count is invalid; use done_traits_menu instead.";
    return false;
  }
  const auto options = GetOptions(menu);
  if (options.empty()) {
    error = "No traits are currently available.";
    return false;
  }
  if (select < 0 || unselect < 0 || static_cast<size_t>(select) >= options.size() ||
      static_cast<size_t>(unselect) >= options.size()) {
    error = ValidOptionsMessage(options);
    return false;
  }
  if (options[select].selected) {
    error = "The select trait is already active. " + ValidOptionsMessage(options);
    return false;
  }
  const bool anySelected =
      std::any_of(options.begin(), options.end(), [](const TraitOption &option) { return option.selected; });
  const bool shouldUnselect = menu->numSelected >= menu->maxSelect && anySelected;
  if (shouldUnselect && !options[unselect].selected) {
    error = "All trait slots are occupied, so unselect must identify a currently selected trait. " +
            ValidOptionsMessage(options);
    return false;
  }
  snapshot = {.owner = menu,
              .selectTile = options[select].tile,
              .unselectTile = shouldUnselect ? options[unselect].tile : nullptr,
              .selectIndex = select,
              .unselectIndex = unselect,
              .shouldUnselect = shouldUnselect,
              .signature = BuildSignature(menu, options)};
  return true;
}

bool RevalidateSelection(const SelectionSnapshot &snapshot, std::string &error) {
  auto *menu = GetMenu();
  if (!menu || !IsTopMenu(menu) || menu->maxSelect == 0 || menu->numSelected > menu->maxSelect || g_execution ||
      !MatchesSelection(snapshot, menu, GetOptions(menu))) {
    error = "The selected traits changed before execution. Choose from the current menu and try again.";
    return false;
  }
  return true;
}

bool ValidateDone(DoneSnapshot &snapshot, std::string &error) {
  auto *menu = GetMenu();
  if (!menu || !IsTopMenu(menu) || g_execution) {
    error = "The traits menu is no longer ready to finish.";
    return false;
  }
  const auto options = GetOptions(menu);
  snapshot = {.owner = menu, .signature = BuildSignature(menu, options)};
  return true;
}

bool RevalidateDone(const DoneSnapshot &snapshot, std::string &error) {
  auto *menu = GetMenu();
  if (!menu || !IsTopMenu(menu) || g_execution || !MatchesDone(snapshot, menu, GetOptions(menu))) {
    error = "The traits menu changed before Done could be activated.";
    return false;
  }
  return true;
}

void StartSelection(const SelectionSnapshot &snapshot) {
  std::string error;
  if (!RevalidateSelection(snapshot, error)) {
    NeuroSDK::SendContext(("Trait selection could not start: " + error).c_str());
    return;
  }
  const auto now = Clock::now();
  g_execution = std::make_unique<ExecutionState>(
      ExecutionState{.selection = snapshot,
                     .startedAt = now,
                     .nextStepAt = now,
                     .phase = snapshot.shouldUnselect ? Phase::PrepareUnselect : Phase::PrepareSelect});
}

void StartDone(const DoneSnapshot &snapshot) {
  std::string error;
  if (!RevalidateDone(snapshot, error)) {
    NeuroSDK::SendContext(("Traits Done could not start: " + error).c_str());
    return;
  }
  const auto now = Clock::now();
  g_execution = std::make_unique<ExecutionState>(ExecutionState{.done = snapshot,
                                                                .startedAt = now,
                                                                .nextStepAt = now + kVisualDelay,
                                                                .phase = Phase::ClickDone,
                                                                .finishing = true});
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
  auto *menu = GetMenu();
  const bool menuOpen = menu && IsTopMenu(menu);
  if (g_execution) {
    if (unobstructed)
      AdvanceExecution();
    return g_execution != nullptr || menuOpen;
  }
  if (Actions::ActionRegistry::Get().HasPendingResult("select_trait") ||
      Actions::ActionRegistry::Get().HasPendingResult("done_traits_menu"))
    return true;
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Ended)
    g_window.reset();
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Closing) {
    CloseWindow();
    return true;
  }

  if (menu && Menu::IsMenuVisible(Interface::Traits) && !menuOpen)
    return true;
  if (!menuOpen || !unobstructed) {
    CloseWindow();
    return menuOpen;
  }
  const auto options = GetOptions(menu);
  if (options.empty()) {
    CloseWindow();
    return true;
  }
  const std::string signature = BuildSignature(menu, options);
  static std::string publishedSignature;
  if (g_window && signature == publishedSignature)
    return true;
  CloseWindow();
  if (g_window)
    return true;

  const auto describedOptions = GetOptions(menu, true);
  const std::string state = BuildState(menu, describedOptions);
  const bool canSelect = menu->maxSelect > 0 && menu->numSelected <= menu->maxSelect;
  const std::string query =
      canSelect ? "Either select one trait with select_trait, or use done_traits_menu to finish now without selecting "
                  "anything else. Traits are optional."
                : "Use done_traits_menu to finish now without selecting a trait. Traits are optional.";
  g_window = std::make_unique<Actions::ActionWindow>();
  g_window->SetContext("Trait choice required. " + state);
  if (canSelect)
    g_window->Add(std::make_unique<Actions::Menu::SelectTraitAction>(options.size()));
  g_window->Add(std::make_unique<Actions::Menu::DoneTraitsMenuAction>())
      .SetForce(query, state, NeuroSDK::ActionPriority::High);
  if (!g_window->Register()) {
    if (g_window->GetState() != Actions::ActionWindow::State::Closing)
      g_window.reset();
    return true;
  }
  publishedSignature = signature;
  _MESSAGE("Opened traits action window with %zu trait(s)", options.size());
  return true;
}

} // namespace Menus::TraitsHandler
