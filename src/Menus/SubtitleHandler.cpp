#include "SubtitleHandler.hpp"
#include "GameObjects.h"
#include "GameUI.h"
#include "Menus/SpeakerMatch.hpp"
#include "Menus/SubtitleTracker.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include "utils/WorldUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
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
    message += "\n- " + (subtitle.speaker.empty() ? "" : subtitle.speaker + ": ") + "\"" + subtitle.text + "\"";

  // Overheard subtitles are background knowledge; dialog lines the player takes part in come from DialogHandler.
  if (NeuroSDK::SendContext(message.c_str(), true)) {
    g_pending.clear();
    g_flushScheduled = false;
  } else {
    g_flushAt = now + kRetryDelay;
  }
}

// Candidates are only those near the source: a subtitle's sound is audible close to its speaker.
constexpr float kCandidateRadius = 600.0f;
constexpr float kMaxBodyScale = 4.0f;

// Who said it: the actor whose body the subtitle's source position lies on. The subtitle data carries no speaker, so
// this is a geometric match that gives no name when it is not clear-cut (see SpeakerMatch). The position is assumed
// to be the speaker's (JohnnyGuitarNVSE documents it as the source position); that is unverified in game, so every
// decision is logged.
std::string ResolveSpeaker(const SubtitleEntry &entry) {
  const SpeakerMatch::Point source{entry.x, entry.y, entry.z};
  std::vector<SpeakerMatch::Candidate> candidates;
  WorldUtils::ForEachLoadedReference([&](TESObjectREFR *ref, TESObjectCELL *) {
    if (!ref->IsActor() || static_cast<Actor *>(ref)->GetDead())
      return;
    const auto &position = ref->GetPos();
    const float dx = position.x - source.x;
    const float dy = position.y - source.y;
    if (std::sqrt(dx * dx + dy * dy) > kCandidateRadius)
      return;
    const char *name = ref->GetFullName();
    const float scale = std::clamp(ref->GetScale(), 0.3f, kMaxBodyScale);
    candidates.push_back(
        {name ? name : "", {position.x, position.y, position.z}, SpeakerMatch::kDefaultBodyHeight * scale});
  });

  const auto result = SpeakerMatch::Resolve(source, candidates);
  _MESSAGE("Subtitle \"%s\" from (%.0f, %.0f, %.0f): speaker %s%s%s (%zu candidate(s), nearest %.0f, next %.0f)",
           entry.text.c_str(), source.x, source.y, source.z, SpeakerMatch::OutcomeName(result.outcome),
           result.name.empty() ? "" : " - ", result.name.c_str(), candidates.size(), result.distance, result.runnerUp);
  return result.name;
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
                           subtitle->priority, subtitle->position.x, subtitle->position.y, subtitle->position.z});
    }
  }

  for (auto &entry : g_tracker.Observe(current)) {
    entry.speaker = ResolveSpeaker(entry);
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
