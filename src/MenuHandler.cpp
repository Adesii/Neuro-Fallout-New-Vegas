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
      if (DelayedGuard::Delay("MenuAcceptCharacterName", 2.5f)) {
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

void HandleTextEdit() {
  auto *interfaces = InterfaceManager::GetSingleton();
  if (!interfaces)
    return;
  // If TextEditMenu is active don't check other things
  if (!handleCharacterNameTextEdit())
    return;
}

void Process() {
  HandleSubtitles();
  HandleTextEdit();
  // auto *interfaces = InterfaceManager::GetSingleton();
  // if (!interfaces)
  //   return;
  //
  // auto *menu = interfaces->activeMenu;
  // if (!menu || !menu->tile)
  //   return;
  //
  // static uint32_t lastMenuID = Interface::NoMenu;
  // if (menu->id == lastMenuID)
  //   return;
  //
  // lastMenuID = menu->id;
  // _MESSAGE("Active menu ID: %u", menu->id);
}
} // namespace MenuHandler
