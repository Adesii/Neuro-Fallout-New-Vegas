#include "Context/ContextGate.hpp"

namespace Context {

ContextGate::Verdict ContextGate::Check(std::string_view text, bool silent, Clock::time_point now) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos)
    return Verdict::Empty;
  const auto trimmed = text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
  if (trimmed == "[]")
    return Verdict::Empty;

  if (m_hasLast && silent == m_lastSilent && now - m_lastSentAt <= kDuplicateWindow && text == m_lastText)
    return Verdict::Duplicate;

  m_lastText = std::string(text);
  m_lastSilent = silent;
  m_lastSentAt = now;
  m_hasLast = true;
  return Verdict::Send;
}

bool ContextGate::ShouldWarnOutage() {
  if (m_outageWarned)
    return false;
  m_outageWarned = true;
  return true;
}

void ContextGate::Connected() {
  m_outageWarned = false;
  m_hasLast = false;
  m_lastText.clear();
}

} // namespace Context
