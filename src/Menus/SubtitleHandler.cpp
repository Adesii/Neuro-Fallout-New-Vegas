#include "SubtitleHandler.hpp"
#include "GameUI.h"
#include "NeuroSDK.hpp"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace Menus::SubtitleHandler {
namespace {

constexpr auto kBatchDelay = std::chrono::seconds(5);
constexpr auto kRetryDelay = std::chrono::seconds(1);
constexpr size_t kMaxRememberedSubtitles = 256;

struct SubtitleEntry {
  std::string text;
  UINT32 startTime = 0;
  INT32 priority = 0;

  bool operator==(const SubtitleEntry &other) const {
    return startTime == other.startTime && priority == other.priority && text == other.text;
  }
};

std::vector<SubtitleEntry> g_remembered;
std::vector<SubtitleEntry> g_pending;
ScopedState::Clock::time_point g_flushAt;
bool g_flushScheduled = false;

void Flush(ScopedState::Clock::time_point now) {
  if (g_pending.empty() || !g_flushScheduled || now < g_flushAt)
    return;

  std::string message = "## Recent subtitles";
  for (const auto &subtitle : g_pending)
    message += "\n- \"" + subtitle.text + "\"";

  if (NeuroSDK::SendContext(message.c_str())) {
    g_pending.clear();
    g_flushScheduled = false;
  } else {
    g_flushAt = now + kRetryDelay;
  }
}

} // namespace

void Process() {
  const auto now = ScopedState::Clock::now();
  auto *hud = HUDMainMenu::GetSingleton();
  if (hud && hud->subtitlesArr.pBuffer) {
    for (UINT32 index = 0; index < hud->subtitlesArr.GetSize(); ++index) {
      auto *subtitle = hud->subtitlesArr.GetAt(index);
      if (!subtitle)
        continue;

      SubtitleEntry entry = {UIUtils::CopyBoundedString(subtitle->text, sizeof(subtitle->text)), subtitle->startTime,
                             subtitle->priority};
      if (entry.text.empty() || std::find(g_remembered.begin(), g_remembered.end(), entry) != g_remembered.end())
        continue;

      g_remembered.push_back(entry);
      g_pending.push_back(std::move(entry));
      if (!g_flushScheduled) {
        g_flushScheduled = true;
        g_flushAt = now + kBatchDelay;
      }
    }
  }

  if (g_remembered.size() > kMaxRememberedSubtitles) {
    const auto excess = g_remembered.size() - kMaxRememberedSubtitles;
    g_remembered.erase(g_remembered.begin(), g_remembered.begin() + excess);
  }
  Flush(now);
}

} // namespace Menus::SubtitleHandler
