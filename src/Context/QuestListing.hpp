#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The engine-free text of the `query_quests` result (todos 41 and 04). GameplayHandler reads the quest log and the
// active quest and feeds them in. Host-tested in tests/QuestListingTests.cpp.
namespace Quests {

// The quest the player already has selected (`player->activeQuest`). `objective` is left empty when it is not known
// which of the quest's objectives is meant; the line then names only the quest.
struct SelectedQuest {
  std::string name;
  std::string objective;
  uint32_t questFormId = 0;
};

// Where a row sits in the listing: higher quest priority first, then by quest, objective and target. The query_quests
// ids are numbered in this order, so ids read top to bottom.
struct OrderKey {
  int priority = 0;
  uint32_t questFormId = 0;
  uint32_t objectiveId = 0;
  uint32_t targetFormId = 0;
};
bool ListedBefore(const OrderKey &left, const OrderKey &right);

// One line of the listing: one target of a displayed objective, with its select_quest id, or (id 0) a displayed
// objective without any usable target (no map marker), which is listed but cannot be selected.
struct Row {
  OrderKey key;
  std::string questName;
  std::string objectiveText;
  int id = 0;
  std::string location; // where the target is; unused when id is 0
};

// "You already have quest X selected." when `selected` is set, otherwise nothing.
std::string SelectedQuestLine(const std::optional<SelectedQuest> &selected);

// The whole result: the selected-quest line (if any) at the top, then "## Current quest objectives" with one
// "### <quest>" heading per quest and its objectives below it. An objective with several targets gets one line per
// target, numbered "place k of n"; one without targets says it has no map marker. `rows` is sorted here with
// ListedBefore (stable), so markerless rows can simply be appended to the catalog rows.
std::string BuildQuestListing(const std::optional<SelectedQuest> &selected, std::vector<Row> rows);

} // namespace Quests
