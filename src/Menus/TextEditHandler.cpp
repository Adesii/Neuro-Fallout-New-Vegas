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
// After this many OK clicks that did not close the menu the retries slow down instead of clicking every 1.5 s forever.
constexpr int kFastClickAttempts = 5;
constexpr auto kSlowCharacterNameDelay = std::chrono::seconds(15);
ScopedState::Delay g_acceptDelay;
int g_clickAttempts = 0;
bool g_missingNameLogged = false;
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
  g_clickAttempts = 0;
  g_missingNameLogged = false;
  g_context.Reset();
}

bool Process() {
  auto *menu = TextEditMenu::Get();
  if (!menu || !menu->tile || !menu->currTextTile || !menu->isActive ||
      !UIUtils::IsTopMenu(menu, Interface::TextEdit)) {
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
    // Nothing can be typed without a name; say so once instead of blocking the menu silently.
    if (!g_missingNameLogged) {
      _WARNING("Cannot fill the character name: %s",
               characterName.empty() ? "NeuroSDK has no character display name" : "the OK button is missing");
      g_missingNameLogged = true;
    }
    g_acceptDelay.Reset();
    return true;
  }
  g_missingNameLogged = false;

  if (menu->currentText.c_str() != characterName) {
    menu->currentText.Set(characterName.c_str());
    menu->cursorIndex = characterName.length();
    g_acceptDelay.Reset();
    return true;
  }

  const auto delay = g_clickAttempts < kFastClickAttempts ? kCharacterNameDelay : kSlowCharacterNameDelay;
  if (g_acceptDelay.Ready(menu, characterName, delay) && UIUtils::ClickTile(menu, menu->okButton)) {
    ++g_clickAttempts;
    _MESSAGE("Accepted character name: %s (click %d)", characterName.c_str(), g_clickAttempts);
    g_acceptDelay.Reset();
  }
  return true;
}

} // namespace Menus::TextEditHandler
