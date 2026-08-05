#include "MenuHandler.hpp"
#include "GameData.h"
#include "GameObjects.h"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include <vector>

namespace MenuHandler {

std::vector<HUDMainMenu::SubtitleData *> sentSubtitles;

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
      // Send the subtitle to the client
      // _MESSAGE("Subtitle: %s", subtitle->text);

      std::string subtitleMessage = "Doc Mitchell: \"" + std::string(subtitle->text) + "\"";
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

void Process() {
  HandleSubtitles();
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
