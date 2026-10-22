#include "MenuHandler.hpp"
#include "GameUI.h"
#include "Menus/CharGenHandler.hpp"
#include "Menus/CharacterEditorHandler.hpp"
#include "Menus/ContainerHandler.hpp"
#include "Menus/DialogHandler.hpp"
#include "Menus/MessagePopupHandler.hpp"
#include "Menus/PipBoyHandler.hpp"
#include "Menus/SpecialAllocationHandler.hpp"
#include "Menus/StubMenuHandler.hpp"
#include "Menus/SubtitleHandler.hpp"
#include "Menus/TextEditHandler.hpp"
#include "Menus/TraitsHandler.hpp"
#include "Menus/VatsHandler.hpp"
#include "Utils/DebugLog.hpp"

namespace MenuHandler {

void Reset() {
  Menus::SpecialAllocationHandler::Reset();
  Menus::DialogHandler::Reset();
  Menus::CharGenHandler::Reset();
  Menus::TraitsHandler::Reset();
  Menus::ContainerHandler::Reset();
  Menus::PipBoyHandler::Reset();
  Menus::MessagePopupHandler::Reset();
  Menus::TextEditHandler::Reset();
  Menus::CharacterEditorHandler::Reset();
  Menus::VatsHandler::Reset();
  Menus::StubMenuHandler::Reset();
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
  // A popup, text prompt or character editor on top owns the controls. Every menu with a published action window is
  // still processed each frame, told it is obstructed, so it closes its window (freeing the process-wide force for
  // the popup's own window) and pauses its timers instead of going stale behind the popup.
  const bool modalPresent = popupPresent || textEditPresent || characterEditorPresent;
  const bool specialBlocksGameplay =
      Menus::SpecialAllocationHandler::Process(!modalPresent && !dialogPresent && !charGenPresent && !traitsPresent);
  const bool specialRunning = Menus::SpecialAllocationHandler::IsExecuting();
  const bool dialogBlocksGameplay = Menus::DialogHandler::Process(!modalPresent && !specialRunning);
  const bool charGenBlocksGameplay = Menus::CharGenHandler::Process(!modalPresent && !specialRunning);
  const bool traitsBlocksGameplay =
      Menus::TraitsHandler::Process(!modalPresent && !specialRunning && !Menus::CharGenHandler::IsExecuting());
  const bool containerBlocksGameplay = Menus::ContainerHandler::Process();
  const bool pipBoyBlocksGameplay = Menus::PipBoyHandler::Process();
  const bool vatsBlocksGameplay =
      Menus::VatsHandler::Process(!popupPresent && !textEditPresent && !characterEditorPresent && !dialogPresent);
  const bool stubMenuBlocksGameplay = Menus::StubMenuHandler::Process();

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
  if (dialogBlocksGameplay)
    return true;
  if (charGenBlocksGameplay)
    return true;
  if (traitsBlocksGameplay)
    return true;
  if (specialBlocksGameplay)
    return true;
  if (containerBlocksGameplay)
    return true;
  if (pipBoyBlocksGameplay)
    return true;
  if (vatsBlocksGameplay)
    return true;
  if (stubMenuBlocksGameplay)
    return true;

  // Unknown menus are not automated yet, but they still pause gameplay automation.
  auto *interfaceManager = InterfaceManager::GetSingleton();
  return interfaceManager && interfaceManager->IsInMenuMode();
}

} // namespace MenuHandler
