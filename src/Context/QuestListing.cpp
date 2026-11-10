#include "QuestListing.hpp"

namespace Quests {

std::string SelectedQuestLine(const std::optional<SelectedQuest> &selected) {
  if (!selected)
    return {};
  std::string line = "You already have quest " + selected->name + " selected";
  if (!selected->objective.empty())
    line += " (current objective: " + selected->objective + ")";
  line += ".";
  return line;
}

std::string BuildQuestListing(const std::optional<SelectedQuest> &selected, const std::vector<std::string> &lines) {
  std::string text;
  if (selected)
    text = SelectedQuestLine(selected) + "\n\n";
  text += "## Current quest objectives";
  if (lines.empty()) {
    text += "\n- None.";
    return text;
  }
  for (const auto &line : lines)
    text += "\n- " + line;
  return text;
}

} // namespace Quests
