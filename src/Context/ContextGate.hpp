#pragma once

#include <chrono>
#include <string>
#include <string_view>

namespace Context {

// Policy in front of the context transport: drops junk, collapses a message repeated back to back, and rate-limits
// the "not connected" warning to once per outage. Engine-free (tests/ContextGateTests.cpp).
class ContextGate {
public:
  using Clock = std::chrono::steady_clock;
  enum class Verdict { Send, Empty, Duplicate };

  // The same text with the same silence as the last message sent is a duplicate for this long.
  static constexpr auto kDuplicateWindow = std::chrono::seconds(3);

  // Call once per message that is about to be sent. A Send verdict also becomes the "last message sent".
  Verdict Check(std::string_view text, bool silent, Clock::time_point now = Clock::now());

  // True the first time it is called during an outage, so the warning is logged once.
  bool ShouldWarnOutage();
  // A connection became ready: re-arm the outage warning and forget the last message so fresh output always passes.
  void Connected();

private:
  std::string m_lastText;
  bool m_lastSilent = false;
  Clock::time_point m_lastSentAt{};
  bool m_hasLast = false;
  bool m_outageWarned = false;
};

} // namespace Context
