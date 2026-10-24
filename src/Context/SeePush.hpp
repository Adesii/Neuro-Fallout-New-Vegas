#pragma once

#include "Context/ObjectPriority.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Context {

// What Neuro is told, unasked, about things that came into range: a throttled summary of only the most notable new ones
// (quest targets, containers, people), never repeating a form id until the space changes, without ids (they could go
// stale before she uses them; she calls query_nearby to act). Engine-free (tests/SeePushTests.cpp); GameplayHandler
// feeds it scans and decides when pushing is allowed at all (no menu, fight or force).
struct SeenObject {
  uint32_t formId = 0;
  Objects::Category category = Objects::Category::None;
  std::string name;
  float distance = 0.0f; // game units
  bool questTarget = false;
};

// The things worth a push: any quest target, containers, and living people. Corpses, loot, doors and clutter are not.
bool IsNotable(Objects::Category category, bool questTarget);

struct SeeMessage {
  std::string text;
  bool silent = true; // only a quest target makes it non-silent
};

class SeePush {
public:
  static constexpr float kMinInterval = 20.0f; // seconds between pushes (to tune after play-testing)
  static constexpr size_t kMaxListed = 5;

  // The space changed (a door, a load): forget what was seen. The interval clock keeps running.
  void Reset();
  // A fresh start (disconnect, load): also restart the interval clock.
  void ResetAll();
  // `inRange` is every object currently in range; entries that are not notable or were already reported are ignored.
  // `dt` is the time since the previous call. Returns the message to send, if one is due.
  std::optional<SeeMessage> Update(const std::vector<SeenObject> &inRange, float dt);

private:
  std::unordered_set<uint32_t> m_seen;
  float m_sinceLastPush = kMinInterval;
};

} // namespace Context
