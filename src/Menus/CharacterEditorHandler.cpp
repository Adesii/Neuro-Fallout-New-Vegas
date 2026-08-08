#include "CharacterEditorHandler.hpp"
#include "GameUI.h"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include <chrono>

namespace Menus::CharacterEditorHandler {
namespace {

constexpr auto kAdvanceDelay = std::chrono::milliseconds(500);
ScopedState::Delay g_advanceDelay;

} // namespace

void Reset() { g_advanceDelay.Reset(); }

bool Process() {
  auto *menu = RaceSexMenu::Get();
  if (!menu || !menu->tile) {
    Reset();
    return false;
  }

  char componentPath[] = "NOGLOW_BRANCH/RSM_Background/RSM_next_button";
  auto *nextButton = menu->tile->GetComponentTile(componentPath);
  if (nextButton && g_advanceDelay.Ready(menu, nextButton->name.c_str(), kAdvanceDelay) &&
      UIUtils::ClickTile(menu, nextButton)) {
    g_advanceDelay.Reset();
  }
  return true;
}

} // namespace Menus::CharacterEditorHandler
