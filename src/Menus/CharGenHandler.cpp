#include "CharGenHandler.hpp"
#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "Actions/Menu/DoneCharGenMenuAction.hpp"
#include "Actions/Menu/SelectSkillAction.hpp"
#include "GameAPI.h"
#include "GameForms.h"
#include "GameTiles.h"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "defs/CharGenMenu.hpp"
#include "utils/UIUtils.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Menus::CharGenHandler {
namespace {

using Clock = std::chrono::steady_clock;
using MenuData = CharacterGeneration::MenuData;

constexpr auto kVisualDelay = std::chrono::milliseconds(1200);
constexpr auto kVerifyDelay = std::chrono::milliseconds(400);
constexpr auto kExecutionTimeout = std::chrono::seconds(15);

struct SkillOption {
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

Menu *AsMenu(MenuData *menu) { return reinterpret_cast<Menu *>(menu); }

bool IsTopMenu(MenuData *menu) {
  auto *interfaceManager = InterfaceManager::GetSingleton();
  return menu &&
         (Interface::GetTopMenuID() == Interface::CharGen ||
          (interfaceManager && interfaceManager->IsInMenuMode() && interfaceManager->activeMenu == AsMenu(menu)));
}

bool IsSkillMenu(MenuData *menu) { return menu && menu->isTagSkills != 0; }

int GetMenuTrait(MenuData *menu, const char *name) {
  auto *value = menu && AsMenu(menu)->tile ? AsMenu(menu)->tile->GetValueName(name) : nullptr;
  return value ? static_cast<int>(value->num) : -1;
}

bool IsSelected(Tile *tile) {
  auto *value = tile ? tile->GetValueName("_selected") : nullptr;
  return value && value->num != 0.0f;
}

std::string GetSkillDescription(const std::string &name) {
  for (UINT32 actorValue = eActorVal_SkillsStart; actorValue <= eActorVal_SkillsEnd; ++actorValue) {
    auto *info = GetActorValueInfo(actorValue);
    const char *fullName = info ? info->fullName.GetFullName() : nullptr;
    if (!fullName || name != fullName)
      continue;
    const char *description = info->description.Get(info, 'CSED');
    return description ? description : "";
  }
  return {};
}

std::vector<SkillOption> GetOptions(MenuData *menu, bool includeDescriptions = false) {
  std::vector<SkillOption> options;
  if (!menu)
    return options;
  for (auto *entry = menu->actorValues.GetHead(); entry; entry = entry->GetNext()) {
    auto *item = entry->GetItem();
    if (!item || !item->tile)
      continue;
    std::string name = UIUtils::GetTileString(item->tile);
    if (name.empty())
      continue;
    options.push_back({.tile = item->tile,
                       .name = name,
                       .description = includeDescriptions ? GetSkillDescription(name) : std::string(),
                       .selected = IsSelected(item->tile)});
  }
  return options;
}

std::string BuildSignature(MenuData *menu, const std::vector<SkillOption> &options) {
  std::string signature =
      std::to_string(GetMenuTrait(menu, "_CurrPoints")) + "/" + std::to_string(GetMenuTrait(menu, "_MaxPoints"));
  for (const auto &option : options)
    signature += "\n" + option.name + (option.selected ? ":1" : ":0");
  return signature;
}

std::string BuildState(MenuData *menu, const std::vector<SkillOption> &options) {
  const bool canFinish =
      GetMenuTrait(menu, "_CurrPoints") >= 0 && GetMenuTrait(menu, "_CurrPoints") == GetMenuTrait(menu, "_MaxPoints");
  std::string state = "## Tag skills\nTagged slots: **" + std::to_string(GetMenuTrait(menu, "_CurrPoints")) + " of " +
                      std::to_string(GetMenuTrait(menu, "_MaxPoints")) + "**.";
  state += canFinish ? " `done_char_gen_menu` is available to keep these skills and finish."
                     : " `done_char_gen_menu` is unavailable until every slot is assigned.";
  state +=
      " `select_skill` requires `select` and `unselect` indexes. `unselect` is ignored while a free slot exists or no "
      "skill is selected, but it must still be a valid skill index.";
  state += "\n\n## Skills";
  for (size_t index = 0; index < options.size(); ++index) {
    state += "\n- `" + std::to_string(index) + "` - **" +
             std::string(options[index].selected ? "selected" : "not selected") + "** - " + options[index].name + ": " +
             options[index].description;
  }
  return state;
}

std::string ValidOptionsMessage(const std::vector<SkillOption> &options) {
  std::string message = "Current skills:";
  for (size_t index = 0; index < options.size(); ++index)
    message += " " + std::to_string(index) + " (" + options[index].name +
               (options[index].selected ? ", selected)" : ", not selected)");
  return message;
}

bool HasTile(const std::vector<SkillOption> &options, Tile *tile) {
  return std::any_of(options.begin(), options.end(), [tile](const SkillOption &option) { return option.tile == tile; });
}

bool MatchesSelection(const SelectionSnapshot &snapshot, MenuData *menu, const std::vector<SkillOption> &options) {
  return menu && menu == snapshot.owner && IsTopMenu(menu) && IsSkillMenu(menu) && snapshot.selectIndex >= 0 &&
         static_cast<size_t>(snapshot.selectIndex) < options.size() &&
         options[snapshot.selectIndex].tile == snapshot.selectTile &&
         (!snapshot.shouldUnselect ||
          (snapshot.unselectIndex >= 0 && static_cast<size_t>(snapshot.unselectIndex) < options.size() &&
           options[snapshot.unselectIndex].tile == snapshot.unselectTile)) &&
         BuildSignature(menu, options) == snapshot.signature;
}

bool MatchesDone(const DoneSnapshot &snapshot, MenuData *menu, const std::vector<SkillOption> &options) {
  return menu && menu == snapshot.owner && IsTopMenu(menu) && IsSkillMenu(menu) &&
         BuildSignature(menu, options) == snapshot.signature && GetMenuTrait(menu, "_CurrPoints") >= 0 &&
         GetMenuTrait(menu, "_CurrPoints") == GetMenuTrait(menu, "_MaxPoints");
}

void CloseWindow() {
  if (g_window && g_window->End())
    g_window.reset();
}

void StopExecution(const std::string &reason) {
  _WARNING("Tag-skill execution stopped: %s", reason.c_str());
  NeuroSDK::SendContext(("Tag-skill selection stopped: " + reason).c_str());
  g_execution.reset();
}

bool SelectVisibly(MenuData *menu, Tile *tile) {
  menu->actorValues.SetSelectedTile(tile);
  menu->actorValues.ScrollToHighlight();
  return menu->actorValues.GetSelectedTile() == tile;
}

void AdvanceExecution() {
  if (!g_execution)
    return;
  auto *menu = CharacterGeneration::GetMenu();
  if (!menu || !Menu::IsMenuVisible(Interface::CharGen)) {
    if (g_execution->finishing)
      _MESSAGE("Tag-skill menu visual execution completed");
    else
      _WARNING("Tag-skill menu closed during visual execution");
    g_execution.reset();
    return;
  }
  if (!IsTopMenu(menu))
    return;
  if (menu != (g_execution->finishing ? g_execution->done.owner : g_execution->selection.owner)) {
    StopExecution("The character generation menu changed unexpectedly.");
    return;
  }
  const auto now = Clock::now();
  if (now - g_execution->startedAt > kExecutionTimeout) {
    StopExecution("The visual skill selection timed out.");
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
      StopExecution("The skill to unselect is no longer available.");
      return;
    }
    execution.phase = Phase::ClickUnselect;
    execution.nextStepAt = now + kVisualDelay;
    _VMESSAGE("Visibly selected skill index %d for removal", execution.selection.unselectIndex);
    return;
  case Phase::ClickUnselect:
    if (menu->actorValues.GetSelectedTile() != execution.selection.unselectTile ||
        !UIUtils::ClickTile(AsMenu(menu), execution.selection.unselectTile)) {
      StopExecution("The selected skill could not be untagged.");
      return;
    }
    execution.phase = Phase::VerifyUnselect;
    execution.nextStepAt = now + kVerifyDelay;
    return;
  case Phase::VerifyUnselect:
    if (IsSelected(execution.selection.unselectTile)) {
      StopExecution("The selected skill remained tagged after its click.");
      return;
    }
    execution.phase = Phase::PrepareSelect;
    execution.nextStepAt = now + kVisualDelay;
    return;
  case Phase::PrepareSelect:
    if ((!execution.selection.shouldUnselect && !MatchesSelection(execution.selection, menu, options)) ||
        !HasTile(options, execution.selection.selectTile) || IsSelected(execution.selection.selectTile) ||
        !SelectVisibly(menu, execution.selection.selectTile)) {
      StopExecution("The new skill is no longer available for selection.");
      return;
    }
    execution.phase = Phase::ClickSelect;
    execution.nextStepAt = now + kVisualDelay;
    _VMESSAGE("Visibly selected skill index %d for tagging", execution.selection.selectIndex);
    return;
  case Phase::ClickSelect:
    if (menu->actorValues.GetSelectedTile() != execution.selection.selectTile ||
        !UIUtils::ClickTile(AsMenu(menu), execution.selection.selectTile)) {
      StopExecution("The new skill could not be tagged.");
      return;
    }
    execution.phase = Phase::VerifySelect;
    execution.nextStepAt = now + kVerifyDelay;
    return;
  case Phase::VerifySelect:
    if (!IsSelected(execution.selection.selectTile)) {
      StopExecution("The new skill was not tagged after its click.");
      return;
    }
    _MESSAGE("Tag-skill visual selection completed");
    g_execution.reset();
    return;
  case Phase::ClickDone:
    if (!MatchesDone(execution.done, menu, options)) {
      StopExecution("The tag-skill menu changed before Done could be activated.");
      return;
    }
    if (!UIUtils::ClickControl(AsMenu(menu), "NOGLOW_BRANCH\\CGM_MainRect\\CGM_ButtonRect\\CGM_DoneButton")) {
      StopExecution("The tag-skill Done button could not be activated.");
      return;
    }
    execution.phase = Phase::WaitClose;
    execution.nextStepAt = now + kVisualDelay;
    _VMESSAGE("Tag-skill Done button activated");
    return;
  case Phase::WaitClose:
    return;
  }
}

} // namespace

bool ValidateSelection(int select, int unselect, SelectionSnapshot &snapshot, std::string &error) {
  auto *menu = CharacterGeneration::GetMenu();
  if (!menu || !IsTopMenu(menu) || !IsSkillMenu(menu) || g_execution) {
    error = "The tag-skill menu is no longer ready.";
    return false;
  }
  const auto options = GetOptions(menu);
  if (options.empty()) {
    error = "No skills are currently available.";
    return false;
  }
  if (select < 0 || unselect < 0 || static_cast<size_t>(select) >= options.size() ||
      static_cast<size_t>(unselect) >= options.size()) {
    error = ValidOptionsMessage(options);
    return false;
  }
  if (options[select].selected) {
    error = "The select skill is already tagged. " + ValidOptionsMessage(options);
    return false;
  }
  const bool anySelected =
      std::any_of(options.begin(), options.end(), [](const SkillOption &option) { return option.selected; });
  const int currentPoints = GetMenuTrait(menu, "_CurrPoints");
  const int maximumPoints = GetMenuTrait(menu, "_MaxPoints");
  if (currentPoints < 0 || maximumPoints <= 0 || currentPoints > maximumPoints) {
    error = "The tag-skill point state is invalid.";
    return false;
  }
  const bool shouldUnselect = currentPoints >= maximumPoints && anySelected;
  if (shouldUnselect && !options[unselect].selected) {
    error = "All tag slots are occupied, so unselect must identify a currently selected skill. " +
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
  auto *menu = CharacterGeneration::GetMenu();
  if (!menu || !IsTopMenu(menu) || !IsSkillMenu(menu) || g_execution ||
      !MatchesSelection(snapshot, menu, GetOptions(menu))) {
    error = "The tagged skills changed before execution. Choose from the current menu and try again.";
    return false;
  }
  return true;
}

bool ValidateDone(DoneSnapshot &snapshot, std::string &error) {
  auto *menu = CharacterGeneration::GetMenu();
  const int currentPoints = GetMenuTrait(menu, "_CurrPoints");
  const int maximumPoints = GetMenuTrait(menu, "_MaxPoints");
  if (!menu || !IsTopMenu(menu) || !IsSkillMenu(menu) || g_execution || currentPoints < 0 ||
      currentPoints != maximumPoints) {
    error = "The tag-skill menu cannot be finished until all tag slots are assigned.";
    return false;
  }
  const auto options = GetOptions(menu);
  snapshot = {.owner = menu, .signature = BuildSignature(menu, options)};
  return true;
}

bool RevalidateDone(const DoneSnapshot &snapshot, std::string &error) {
  auto *menu = CharacterGeneration::GetMenu();
  if (!menu || !IsTopMenu(menu) || !IsSkillMenu(menu) || g_execution ||
      !MatchesDone(snapshot, menu, GetOptions(menu))) {
    error = "The tag-skill menu changed before Done could be activated.";
    return false;
  }
  return true;
}

void StartSelection(const SelectionSnapshot &snapshot) {
  std::string error;
  if (!RevalidateSelection(snapshot, error)) {
    NeuroSDK::SendContext(("Skill selection could not start: " + error).c_str());
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
    NeuroSDK::SendContext(("Tag-skill Done could not start: " + error).c_str());
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
  auto *menu = CharacterGeneration::GetMenu();
  const bool menuOpen = menu && IsTopMenu(menu) && IsSkillMenu(menu);
  if (g_execution) {
    if (unobstructed)
      AdvanceExecution();
    return g_execution != nullptr || menuOpen;
  }
  if (Actions::ActionRegistry::Get().HasPendingResult("select_skill") ||
      Actions::ActionRegistry::Get().HasPendingResult("done_char_gen_menu"))
    return true;
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Ended)
    g_window.reset();
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Closing) {
    CloseWindow();
    return true;
  }

  if (menu && Menu::IsMenuVisible(Interface::CharGen) && !menuOpen)
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
  const bool canFinish =
      GetMenuTrait(menu, "_CurrPoints") >= 0 && GetMenuTrait(menu, "_CurrPoints") == GetMenuTrait(menu, "_MaxPoints");
  const std::string query = canFinish
                                ? "Either change one tagged skill with select_skill, or use done_char_gen_menu to keep "
                                  "the current selection and finish now."
                                : "Change one tagged skill with select_skill. done_char_gen_menu is unavailable until "
                                  "all tag slots are assigned.";
  g_window = std::make_unique<Actions::ActionWindow>();
  g_window->Add(std::make_unique<Actions::Menu::SelectSkillAction>(options.size()));
  if (canFinish)
    g_window->Add(std::make_unique<Actions::Menu::DoneCharGenMenuAction>());
  g_window->SetForce(query, state, NeuroSDK::ActionPriority::Medium);
  if (!g_window->Register()) {
    if (g_window->GetState() != Actions::ActionWindow::State::Closing)
      g_window.reset();
    return true;
  }
  publishedSignature = signature;
  _MESSAGE("Opened tag-skill action window with %zu skill(s)", options.size());
  return true;
}

} // namespace Menus::CharGenHandler
