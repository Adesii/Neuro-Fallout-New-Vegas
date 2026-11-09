#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The engine-free half of `query_quests` (todo 41): which listed objectives belong to the selected quest, and the text.
// GameplayHandler reads the engine (active quest, what select_quest chose, whether the route runs) and feeds it in.
// Host-tested in tests/QuestListingTests.cpp.
namespace QuestListing {

// One listed objective as GameplayHandler catalogued it (the ids are form ids / objective ids).
struct Candidate {
  uint32_t questFormId = 0;
  uint32_t objectiveId = 0;
  uint32_t targetFormId = 0;
};

// What select_quest last chose.
struct Chosen {
  uint32_t questFormId = 0;
  uint32_t objectiveId = 0;
  uint32_t targetFormId = 0;
};

// Which candidates to tag "(selected)": the one select_quest chose; if its exact target is gone from the list, every
// target of the chosen objective. Without a choice for the active quest (it was picked in the Pip-Boy, or the choice
// belongs to another quest) nothing is tagged individually. `activeQuestFormId` 0 means no active quest: nothing.
std::vector<bool> MarkSelected(const std::vector<Candidate> &candidates, uint32_t activeQuestFormId,
                               const std::optional<Chosen> &chosen);

struct ActiveQuest {
  std::string name;
  bool routeRunning = false; // do_current_quest is following it right now
};

struct Entry {
  int id = 0;
  std::string description;
  std::string location;
  bool selected = false;
};

// The "## Current quest objectives" text. With an active quest the first line under the heading says so; the list
// lines are otherwise unchanged. State only, apart from naming do_current_quest.
std::string Format(const std::vector<Entry> &entries, const std::optional<ActiveQuest> &active);

} // namespace QuestListing
