#include "SubtitleHandler.hpp"
#include "GameUI.h"
#include "Menus/SubtitleTracker.hpp"
#include "NeuroSDK.hpp"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace Menus::SubtitleHandler {
namespace {

constexpr auto kBatchDelay = std::chrono::seconds(5);
constexpr auto kRetryDelay = std::chrono::seconds(1);
constexpr size_t kMaxPendingSubtitles = 64;

SubtitleTracker g_tracker;
std::vector<SubtitleEntry> g_pending;
ScopedState::Clock::time_point g_flushAt;
bool g_flushScheduled = false;

void Flush(ScopedState::Clock::time_point now) {
  if (g_pending.empty() || !g_flushScheduled || now < g_flushAt)
    return;

  std::string message = "## Recent subtitles";
  for (const auto &subtitle : g_pending)
    message += "\n- \"" + subtitle.text + "\"";

  // Overheard subtitles are background knowledge; dialog lines the player takes part in come from DialogHandler.
  if (NeuroSDK::SendContext(message.c_str(), true)) {
    g_pending.clear();
    g_flushScheduled = false;
  } else {
    g_flushAt = now + kRetryDelay;
  }
}

} // namespace

void Reset() {
  g_tracker.Reset();
  g_pending.clear();
  g_flushScheduled = false;
}

void Process() {
  const auto now = ScopedState::Clock::now();
  std::vector<SubtitleEntry> current;
  auto *hud = HUDMainMenu::GetSingleton();
  if (hud && hud->subtitlesArr.pBuffer) {
    for (UINT32 index = 0; index < hud->subtitlesArr.GetSize(); ++index) {
      auto *subtitle = hud->subtitlesArr.GetAt(index);
      if (subtitle)
        current.push_back({UIUtils::CopyBoundedString(subtitle->text, sizeof(subtitle->text)), subtitle->startTime,
                           subtitle->priority});
    }
  }

  for (auto &entry : g_tracker.Observe(current)) {
    g_pending.push_back(std::move(entry));
    if (g_pending.size() > kMaxPendingSubtitles)
      g_pending.erase(g_pending.begin());
    if (!g_flushScheduled) {
      g_flushScheduled = true;
      g_flushAt = now + kBatchDelay;
    }
  }
  Flush(now);
}

} // namespace Menus::SubtitleHandler
