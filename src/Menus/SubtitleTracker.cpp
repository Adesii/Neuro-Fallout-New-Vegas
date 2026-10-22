#include "Menus/SubtitleTracker.hpp"
#include <algorithm>

namespace Menus {

std::vector<SubtitleEntry> SubtitleTracker::Observe(const std::vector<SubtitleEntry> &current) {
  std::vector<SubtitleEntry> fresh;
  for (const auto &entry : current) {
    if (entry.text.empty())
      continue;
    const bool known = std::find(m_previous.begin(), m_previous.end(), entry) != m_previous.end();
    const bool duplicate = std::find(fresh.begin(), fresh.end(), entry) != fresh.end();
    if (!known && !duplicate)
      fresh.push_back(entry);
  }
  m_previous = current;
  return fresh;
}

} // namespace Menus
