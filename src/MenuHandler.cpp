#include "MenuHandler.hpp"
#include "GameUI.h"
#include "Menus/CharGenHandler.hpp"
#include "Menus/CharacterEditorHandler.hpp"
#include "Menus/ContainerHandler.hpp"
#include "Menus/DialogHandler.hpp"
#include "Menus/MessagePopupHandler.hpp"
#include "Menus/SpecialAllocationHandler.hpp"
#include "Menus/SubtitleHandler.hpp"
#include "Menus/TextEditHandler.hpp"
#include "Menus/TraitsHandler.hpp"
#include "Utils/DebugLog.hpp"

namespace MenuHandler {

void Reset() {
  Menus::SpecialAllocationHandler::Reset();
  Menus::DialogHandler::Reset();
  Menus::CharGenHandler::Reset();
  Menus::TraitsHandler::Reset();
  Menus::ContainerHandler::Reset();
  Menus::TextEditHandler::Reset();
  Menus::CharacterEditorHandler::Reset();
}

bool Process() {
  // Observation is independent of modal menu handling so subtitles are not lost behind a popup.
  Menus::SubtitleHandler::Process();
  Menus::DialogHandler::Observe();

  const UINT32 topMenu = Interface::GetTopMenuID();
  static UINT32 lastTopMenu = Interface::NoMenu;
  if (topMenu != lastTopMenu) {
    _DMESSAGE("MenuHandler top menu changed: %u -> %u", lastTopMenu, topMenu);
    lastTopMenu = topMenu;
  }
  const bool popupPresent = topMenu == Interface::Message && !StartMenu::Get();
  const bool textEditPresent = topMenu == Interface::TextEdit;
  const bool characterEditorPresent = topMenu == Interface::RaceMenu;
  const bool dialogPresent = topMenu == Interface::Dialog;
  const bool charGenPresent = topMenu == Interface::CharGen;
  const bool traitsPresent = topMenu == Interface::Traits;
  const bool specialBlocksGameplay =
      Menus::SpecialAllocationHandler::Process(!popupPresent && !textEditPresent && !characterEditorPresent &&
                                               !dialogPresent && !charGenPresent && !traitsPresent);
  const bool containerBlocksGameplay = Menus::ContainerHandler::Process();

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
  if (Menus::DialogHandler::Process(!Menus::SpecialAllocationHandler::IsExecuting()))
    return true;
  if (Menus::CharGenHandler::Process(!Menus::SpecialAllocationHandler::IsExecuting()))
    return true;
  if (Menus::TraitsHandler::Process(!Menus::SpecialAllocationHandler::IsExecuting() &&
                                    !Menus::CharGenHandler::IsExecuting()))
    return true;
  if (specialBlocksGameplay)
    return true;
  if (containerBlocksGameplay)
    return true;

  // Unknown menus are not automated yet, but they still pause gameplay automation.
  auto *interfaceManager = InterfaceManager::GetSingleton();
  return interfaceManager && interfaceManager->IsInMenuMode();
}

} // namespace MenuHandler
