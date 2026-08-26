#include "TextEditHandler.hpp"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include <chrono>
#include <string>

namespace Menus::TextEditHandler {
namespace {

constexpr auto kCharacterNameDelay = std::chrono::milliseconds(1500);
ScopedState::Delay g_acceptDelay;
ScopedState::Observation g_context;

void SendContextOnce(TextEditMenu *menu, const std::string &prompt) {
  if (g_context.IsCurrent(menu, prompt))
    return;
  if (NeuroSDK::SendContext(("## Text input\n" + prompt).c_str()))
    g_context.Commit(menu, prompt);
}

} // namespace

void Reset() {
  g_acceptDelay.Reset();
  g_context.Reset();
}

bool Process() {
  auto *menu = TextEditMenu::Get();
  if (!menu || !menu->tile || !menu->currTextTile || !menu->isActive) {
    Reset();
    return false;
  }

  char componentPath[] = "TEM_MainRect/textedit_prompt/string";
  auto *promptValue = menu->tile->GetComponentValue(componentPath);
  const std::string prompt = promptValue && promptValue->str ? promptValue->str : "Text input requested.";
  SendContextOnce(menu, prompt);

  if (prompt != "Enter character name.") {
    g_acceptDelay.Reset();
    return true;
  }

  const std::string characterName = NeuroSDK::GetCharacterDisplayName();
  if (characterName.empty() || !menu->okButton) {
    g_acceptDelay.Reset();
    return true;
  }

  if (menu->currentText.c_str() != characterName) {
    menu->currentText.Set(characterName.c_str());
    menu->cursorIndex = characterName.length();
    g_acceptDelay.Reset();
    return true;
  }

  if (g_acceptDelay.Ready(menu, characterName, kCharacterNameDelay) && UIUtils::ClickTile(menu, menu->okButton)) {
    _MESSAGE("Accepted character name: %s", characterName.c_str());
    g_acceptDelay.Reset();
  }
  return true;
}

} // namespace Menus::TextEditHandler
