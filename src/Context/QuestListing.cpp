#include "QuestListing.hpp"
#include <algorithm>
#include <cstddef>
#include <utility>

namespace Quests {

bool ListedBefore(const OrderKey &left, const OrderKey &right) {
  if (left.priority != right.priority)
    return left.priority > right.priority;
  if (left.questFormId != right.questFormId)
    return left.questFormId < right.questFormId;
  if (left.objectiveId != right.objectiveId)
    return left.objectiveId < right.objectiveId;
  return left.targetFormId < right.targetFormId;
}

std::string SelectedQuestLine(const std::optional<SelectedQuest> &selected) {
  if (!selected)
    return {};
  std::string line = "You already have quest " + selected->name + " selected";
  if (!selected->objective.empty())
    line += " (current objective: " + selected->objective + ")";
  line += ".";
  return line;
}

namespace {

bool SameObjective(const Row &left, const Row &right) {
  return left.key.questFormId == right.key.questFormId && left.key.objectiveId == right.key.objectiveId;
}

} // namespace

std::string BuildQuestListing(const std::optional<SelectedQuest> &selected, std::vector<Row> rows) {
  std::stable_sort(rows.begin(), rows.end(),
                   [](const Row &left, const Row &right) { return ListedBefore(left.key, right.key); });

  std::string text;
  if (selected)
    text = SelectedQuestLine(selected) + "\n\n";
  text += "## Current quest objectives";
  if (rows.empty()) {
    text += "\n- None.";
    return text;
  }

  for (size_t index = 0; index < rows.size();) {
    const Row &first = rows[index];
    // One objective: rows[index, end) share quest and objective.
    size_t end = index + 1;
    while (end < rows.size() && SameObjective(rows[end], first))
      ++end;

    if (index == 0 || rows[index - 1].key.questFormId != first.key.questFormId) {
      text += "\n\n### " + first.questName;
      if (selected && selected->questFormId == first.key.questFormId)
        text += " (selected)";
    }

    const size_t targets = static_cast<size_t>(
        std::count_if(rows.begin() + index, rows.begin() + end, [](const Row &row) { return row.id != 0; }));
    if (targets == 0) {
      text += "\n- " + first.objectiveText + " - no map marker, so it has no id to select or follow";
    } else {
      size_t place = 0;
      for (size_t row = index; row < end; ++row) {
        if (rows[row].id == 0)
          continue;
        ++place;
        text += "\n- `" + std::to_string(rows[row].id) + "` - " + rows[row].objectiveText;
        if (targets > 1)
          text += " (place " + std::to_string(place) + " of " + std::to_string(targets) + ")";
        text += " - " + rows[row].location;
      }
    }
    index = end;
  }
  return text;
}

} // namespace Quests
