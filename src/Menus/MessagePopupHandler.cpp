#include "MessagePopupHandler.hpp"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace Menus::MessagePopupHandler {
namespace {

constexpr auto kAcceptDelay = std::chrono::milliseconds(1500);
ScopedState::Delay g_acceptDelay;
ScopedState::Observation g_context;

struct PopupButton {
  Tile *tile = nullptr;
  std::string label;
};

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

void Reset() {
  g_acceptDelay.Reset();
  g_context.Reset();
}

} // namespace

bool Process() {
  auto *menu = MessageMenu::Get();
  if (!menu || !menu->tile || StartMenu::Get()) {
    Reset();
    return false;
  }

  const std::string title = UIUtils::GetTileString(menu->titleTile);
  const std::string text = UIUtils::GetTileString(menu->messageText);
  const auto buttons = GetButtons(menu);

  std::string signature = title + "\n" + text;
  std::string context = "Message popup: " + title + "\n" + text;
  if (!buttons.empty()) {
    context += "\nOptions:";
    for (const auto &button : buttons) {
      signature += "\n" + button.label;
      context += "\n- " + button.label;
    }
  }
  if (!g_context.IsCurrent(menu, signature) && NeuroSDK::SendContext(context.c_str()))
    g_context.Commit(menu, signature);

  Tile *automaticChoice = nullptr;
  if (buttons.size() == 1) {
    automaticChoice = buttons.front().tile;
  } else {
    auto yes =
        std::find_if(buttons.begin(), buttons.end(), [](const PopupButton &button) { return button.label == "Yes"; });
    if (yes != buttons.end()) {
      automaticChoice = yes->tile;
      menu->buttonList.SetSelectedTile(automaticChoice);
    }
  }

  if (automaticChoice && g_acceptDelay.Ready(menu, signature, kAcceptDelay) &&
      UIUtils::ClickTile(menu, automaticChoice)) {
    g_acceptDelay.Reset();
  }
  return true;
}

} // namespace Menus::MessagePopupHandler
