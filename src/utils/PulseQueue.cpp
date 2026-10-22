#include "utils/InputUtils.hpp"
#include <algorithm>

// Engine-free: it reaches the game only through InputUtils::SetControlHeld, which the host tests define themselves.

namespace InputUtils {

void PulseQueue::Pulse(uint32_t macro, float seconds) {
  if (macro == kNoControl)
    return;
  SetControlHeld(m_owner, macro, true);
  auto existing =
      std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry &entry) { return entry.macro == macro; });
  if (existing != m_entries.end()) {
    existing->remaining = std::max(existing->remaining, seconds);
    return;
  }
  m_entries.push_back({macro, seconds});
}

void PulseQueue::Advance(float deltaTime) {
  for (auto &entry : m_entries) {
    entry.remaining -= deltaTime;
    if (entry.remaining <= 0.0f)
      SetControlHeld(m_owner, entry.macro, false);
  }
  std::erase_if(m_entries, [](const Entry &entry) { return entry.remaining <= 0.0f; });
}

void PulseQueue::ReleaseAll() {
  for (const auto &entry : m_entries)
    SetControlHeld(m_owner, entry.macro, false);
  m_entries.clear();
}

} // namespace InputUtils
