#include "MenuHandler.hpp"
#include "GameUI.h"
#include "Menus/CharacterEditorHandler.hpp"
#include "Menus/MessagePopupHandler.hpp"
#include "Menus/SubtitleHandler.hpp"
#include "Menus/TextEditHandler.hpp"

namespace MenuHandler {

bool Process() {
  // Observation is independent of modal menu handling so subtitles are not lost behind a popup.
  Menus::SubtitleHandler::Process();

  if (Menus::MessagePopupHandler::Process()) {
    Menus::TextEditHandler::Reset();
    Menus::CharacterEditorHandler::Reset();
    return true;
  }
  if (Menus::TextEditHandler::Process()) {
    Menus::CharacterEditorHandler::Reset();
    return true;
  }
  if (Menus::CharacterEditorHandler::Process())
    return true;

  // Unknown menus are not automated yet, but they still pause gameplay automation.
  auto *interfaceManager = InterfaceManager::GetSingleton();
  return interfaceManager && interfaceManager->IsInMenuMode();
}

} // namespace MenuHandler
