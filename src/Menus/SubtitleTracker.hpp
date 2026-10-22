#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Menus {

struct SubtitleEntry {
  std::string text;
  uint32_t startTime = 0;
  int32_t priority = 0;

  bool operator==(const SubtitleEntry &other) const = default;
};

// Tells newly shown subtitles from ones already reported. The HUD keeps a subtitle in its array while it is
// displayed, so an entry is "the same" only while it stays in the array: once it leaves, it is forgotten. A line
// repeated later, or a start time that restarted after a load, is therefore reported again. Engine-free.
class SubtitleTracker {
public:
  // `current` is the HUD's subtitle array this frame. Returns the entries not present at the previous observation.
  std::vector<SubtitleEntry> Observe(const std::vector<SubtitleEntry> &current);
  void Reset() { m_previous.clear(); }

private:
  std::vector<SubtitleEntry> m_previous;
};

} // namespace Menus
