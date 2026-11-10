#pragma once

#include <optional>
#include <string>
#include <vector>

// The engine-free text of the `query_quests` result (todo 41). GameplayHandler reads the quest log and the active
// quest and feeds them in. Host-tested in tests/QuestListingTests.cpp.
namespace Quests {

// The quest the player already has selected (`player->activeQuest`). `objective` is left empty when it is not known
// which of the quest's objectives is meant; the line then names only the quest.
struct SelectedQuest {
  std::string name;
  std::string objective;
};

// "You already have quest X selected." when `selected` is set, otherwise nothing.
std::string SelectedQuestLine(const std::optional<SelectedQuest> &selected);

// The whole result: the selected-quest line (if any) at the top, then the "## Current quest objectives" list, one
// bullet per entry in `lines` (each line is the text after "- "), or "- None." when there are none.
std::string BuildQuestListing(const std::optional<SelectedQuest> &selected, const std::vector<std::string> &lines);

} // namespace Quests
