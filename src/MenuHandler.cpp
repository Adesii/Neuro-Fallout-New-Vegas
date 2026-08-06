#include "MenuHandler.hpp"
#include "GameData.h"
#include "GameObjects.h"
#include "GameTiles.h"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "hooks/Hooks_DirectInput8Create.h"
#include "itr/internal/GameGlobals.h"
#include "utils/DelayedGuard.hpp"
#include <cstring>
#include <vector>

namespace MenuHandler {

std::vector<HUDMainMenu::SubtitleData *> sentSubtitles;

// TODO: aggregate subtitles and send them together after 5 seconds of the first subtitle.
void HandleSubtitles() {
  auto hudmainmenu = HUDMainMenu::GetSingleton();
  if (!hudmainmenu || hudmainmenu->subtitlesArr.pBuffer == nullptr)
    return;

  // _MESSAGE("Handling subtitles. Current subtitle count: %zu", hudmainmenu->subtitlesArr.GetSize());
  if (hudmainmenu->subtitlesArr.GetSize() == 0) {
    sentSubtitles.clear();
    return;
  }

  float currentTime = TimeGlobal::Get()->secondsPassed;

  std::vector<HUDMainMenu::SubtitleData *> subtitlesToSend;
  hudmainmenu->subtitlesArr.ForEach([&](HUDMainMenu::SubtitleData *subtitle) {
    if (!subtitle)
      return;

    subtitlesToSend.push_back(subtitle);
  });

  for (auto subtitle : subtitlesToSend) {
    if (std::find(sentSubtitles.begin(), sentSubtitles.end(), subtitle) == sentSubtitles.end()) {
      std::string subtitleMessage = "Subtitle: \"" + std::string(subtitle->text) + "\"";
      NeuroSDK::SendContext(subtitleMessage.data());
      sentSubtitles.push_back(subtitle);
    }
  }

  // clear stale subtitles from the sentSubtitles vector
  for (auto iter = sentSubtitles.begin(); iter != sentSubtitles.end();) {
    auto subtitle = *iter;
    if (!subtitle)
      continue;

    if (std::find(subtitlesToSend.begin(), subtitlesToSend.end(), subtitle) == subtitlesToSend.end()) {
      iter = sentSubtitles.erase(iter);
    } else {
      ++iter;
    }
  }
}

bool handleCharacterNameTextEdit() {

  auto textedit = TextEditMenu::Get();
  if (!textedit || !textedit->tile || !textedit->currTextTile || !textedit->currTextTile || !textedit->isActive)
    return false;
  // TODO: send action force for text edit if its not a Character name text edit. At the start of the game.
  // Also if there was no StartUp acknowledgement so we don't know who is playing

  // GetComponentValue temporarily writes null terminators into the path.
  char componentPath[] = "TEM_MainRect/textedit_prompt/string";
  auto value = textedit->tile->GetComponentValue(componentPath);
  if (!value || !value->str)
    return false;

  if (std::strcmp(value->str, "Enter character name.") == 0) {
    auto characterName = NeuroSDK::GetCharacterDisplayName();
    if (std::strcmp(textedit->currentText.c_str(), characterName.c_str()) == 0) {
      _MESSAGE("Character name is already set to \"%s\", Accepting it.", characterName.c_str());
      _MESSAGE("Current time: %f, Remaining delay: %f", TimeGlobal::Get()->secondsPassed,
               DelayedGuard::GetRemainingDelay("MenuAcceptCharacterName"));
      if (DelayedGuard::Delay("MenuAcceptCharacterName", 1.5f)) {
        textedit->HandleClick(textedit->okButton->GetValue(kTileValue_id)->num, textedit->okButton);
        DelayedGuard::Reset("MenuAcceptCharacterName");
      }
      return true;
    }
    textedit->currentText.Set(characterName.c_str()); // TODO: chcek if Chraacte rnaem is empty..
    textedit->cursorIndex = characterName.length();
    return true;
  }
  // TODO: handle other text edits

  return false;
}

bool HandleTextEdit() {
  // If TextEditMenu is active don't check other things
  if (!handleCharacterNameTextEdit())
    return false;
  return true;
}

bool HandleCharacterEditor() {

  auto menu = RaceSexMenu::Get();
  if (!menu || !menu->tile)
    return false;
  // We are in character editor
  // TODO: actually allow the SDK to control the character editor. For now just skip past it by pressing ok all the time
  // Debug_DumpMenus();
  // menu->tile->Dump(false, true);

  // _MESSAGE("Menu tile name: %s", menu->tile->name.c_str());
  char componentPath[] = "NOGLOW_BRANCH/RSM_Background/RSM_next_button"; /// RSM_Background/RSM_next_button
  auto okButton = menu->tile->GetComponentTile(componentPath);
  // _MESSAGE("Ok button tile name: %s", okButton ? okButton->name.c_str() : "null");
  if (okButton) {
    // _MESSAGE("Character editor detected, clicking ok button.");
    if (DelayedGuard::Delay("MenuAcceptCharacterEditor", 0.5f)) {
      menu->HandleClick(okButton->GetValue(kTileValue_id)->num, okButton);
      DelayedGuard::Reset("MenuAcceptCharacterEditor");
    }
  }
  return true;
}

bool HandleMessagePopup() {
  auto menu = MessageMenu::Get();
  bool isinStartMenu = StartMenu::Get() != nullptr;
  if (!menu || !menu->tile || isinStartMenu)
    return false;

  if (menu->buttonList.itemCount == 1) {
    auto buttonTile = menu->buttonList.GetNthTile(0);
    if (buttonTile) {
      if (DelayedGuard::Delay("MenuAcceptMessagePopup", 1.5f)) {
        _MESSAGE("Message popup detected, clicking ok button.");
        // Get Data about the MessageMenu and send it
        std::string messageTitle = menu->titleTile->GetValue(kTileValue_string)->str;
        std::string messageText = menu->messageText->GetValue(kTileValue_string)->str;
        NeuroSDK::SendContext(("MessagePopup: " + messageTitle + " \n " + messageText).data());
        menu->HandleClick(buttonTile->GetValue(kTileValue_id)->num, buttonTile);
        DelayedGuard::Reset("MenuAcceptMessagePopup");
      }
    }
  } else {
    // find Yes and click it
    auto buttonList = menu->buttonList.GetHead();
    while (buttonList) {
      auto buttonItem = buttonList->GetNext();
      if (buttonItem && buttonItem->m_item->tile) {
        auto buttonTile = buttonItem->m_item->tile;
        char componentPath[] = "string";
        auto value = buttonTile->GetComponentValue(componentPath);
        _MESSAGE("Message popup detected, button text: %s", value && value->str ? value->str : "null");
        if (value && value->str && std::strcmp(value->str, "Yes") == 0) {
          // TODO: give the SDK the ability to choose which button to click. For now just click Yes
          menu->buttonList.SetSelectedTile(buttonTile);
          if (DelayedGuard::Delay("MenuAcceptMessagePopup", 1.5f)) {
            _MESSAGE("Message popup detected, clicking Yes button.");
            std::string messageTitle = menu->titleTile->GetValue(kTileValue_string)->str;
            std::string messageText = menu->messageText->GetValue(kTileValue_string)->str;
            menu->HandleClick(buttonTile->GetValue(kTileValue_id)->num, buttonTile);
            DelayedGuard::Reset("MenuAcceptMessagePopup");
          }
          break;
        }
      }
    }
  }

  return true;
}

void Process() {
  if (HandleMessagePopup())
    return;
  HandleSubtitles();
  if (HandleTextEdit())
    return;
  if (HandleCharacterEditor())
    return;
}
} // namespace MenuHandler
