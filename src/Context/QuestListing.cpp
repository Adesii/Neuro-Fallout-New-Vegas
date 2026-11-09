#include "Context/QuestListing.hpp"
#include <sstream>

namespace QuestListing {

std::vector<bool> MarkSelected(const std::vector<Candidate> &candidates, uint32_t activeQuestFormId,
                               const std::optional<Chosen> &chosen) {
  std::vector<bool> marks(candidates.size(), false);
  if (activeQuestFormId == 0 || !chosen || chosen->questFormId != activeQuestFormId)
    return marks;
  bool exact = false;
  for (size_t index = 0; index < candidates.size(); ++index) {
    const Candidate &candidate = candidates[index];
    if (candidate.questFormId == chosen->questFormId && candidate.objectiveId == chosen->objectiveId &&
        candidate.targetFormId == chosen->targetFormId) {
      marks[index] = true;
      exact = true;
    }
  }
  if (exact)
    return marks;
  for (size_t index = 0; index < candidates.size(); ++index)
    marks[index] =
        candidates[index].questFormId == chosen->questFormId && candidates[index].objectiveId == chosen->objectiveId;
  return marks;
}

std::string Format(const std::vector<Entry> &entries, const std::optional<ActiveQuest> &active) {
  std::ostringstream context;
  context << "## Current quest objectives";
  if (active) {
    context << "\nYou already have quest " << active->name << " selected. ";
    context << (active->routeRunning ? "do_current_quest is following it."
                                     : "You are not following it right now; do_current_quest follows it.");
  }
  if (entries.empty()) {
    context << "\n- None.";
    return context.str();
  }
  for (const auto &entry : entries) {
    context << "\n- `" << entry.id << "`" << (entry.selected ? " (selected)" : "") << " - " << entry.description
            << " - " << entry.location;
  }
  return context.str();
}

} // namespace QuestListing
