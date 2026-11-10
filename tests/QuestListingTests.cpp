#include "Context/QuestListing.hpp"
#include "TestFramework.hpp"
#include <optional>
#include <string>
#include <vector>

using namespace Quests;

namespace {

constexpr uint32_t kKick = 0x100;
constexpr uint32_t kSaddle = 0x200;

Row Target(int priority, uint32_t quest, const char *questName, uint32_t objective, const char *text, uint32_t target,
           int id, const char *location) {
  return {{priority, quest, objective, target}, questName, text, id, location};
}

Row Markerless(int priority, uint32_t quest, const char *questName, uint32_t objective, const char *text) {
  return {{priority, quest, objective, 0}, questName, text, 0, {}};
}

} // namespace

TEST("quest listing: nothing selected adds no line") {
  CHECK(SelectedQuestLine(std::nullopt).empty());
  CHECK_EQ(BuildQuestListing(std::nullopt, {}), std::string("## Current quest objectives\n- None."));
  CHECK_EQ(BuildQuestListing(std::nullopt, {Target(50, kKick, "Ain't That a Kick", 10, "Talk to Sunny", 1, 1,
                                                   "in your current area, 12 m away")}),
           std::string("## Current quest objectives\n\n### Ain't That a Kick\n"
                       "- `1` - Talk to Sunny - in your current area, 12 m away"));
}

TEST("quest listing: a selected quest leads the result and its heading is marked") {
  const SelectedQuest selected{"Ain't That a Kick", "Talk to Sunny", kKick};
  const std::string line = "You already have quest Ain't That a Kick selected (current objective: Talk to Sunny).";
  CHECK_EQ(SelectedQuestLine(selected), line);
  const std::vector<Row> rows{Target(50, kKick, "Ain't That a Kick", 10, "Talk to Sunny", 1, 1, "here"),
                              Target(40, kSaddle, "Back in the Saddle", 10, "Find Sunny", 2, 2, "there")};
  CHECK_EQ(BuildQuestListing(selected, rows),
           line + "\n\n## Current quest objectives\n\n### Ain't That a Kick (selected)\n- `1` - Talk to Sunny - here"
                  "\n\n### Back in the Saddle\n- `2` - Find Sunny - there");
}

TEST("quest listing: an unknown objective names only the quest") {
  CHECK_EQ(SelectedQuestLine(SelectedQuest{"Ghost Town Gunfight", "", 1}),
           std::string("You already have quest Ghost Town Gunfight selected."));
}

TEST("quest listing: objectives are grouped under their quest, several targets are numbered places") {
  const std::vector<Row> rows{
      Target(50, kKick, "Ain't That a Kick", 10, "Talk to Sunny", 1, 1, "here"),
      Target(50, kKick, "Ain't That a Kick", 20, "Kill the geckos", 5, 2, "north"),
      Target(50, kKick, "Ain't That a Kick", 20, "Kill the geckos", 6, 3, "south"),
  };
  CHECK_EQ(
      BuildQuestListing(std::nullopt, rows),
      std::string("## Current quest objectives\n\n### Ain't That a Kick\n- `1` - Talk to Sunny - here\n"
                  "- `2` - Kill the geckos (place 1 of 2) - north\n- `3` - Kill the geckos (place 2 of 2) - south"));
}

TEST("quest listing: an objective without a marker is listed in order but has no id") {
  // Appended after the catalog rows, as GameplayHandler does; the sort puts it under its quest.
  const std::vector<Row> rows{
      Target(50, kKick, "Ain't That a Kick", 10, "Talk to Sunny", 1, 1, "here"),
      Target(40, kSaddle, "Back in the Saddle", 10, "Find Sunny", 2, 2, "there"),
      Markerless(50, kKick, "Ain't That a Kick", 30, "Ask around about the Courier"),
      Markerless(10, 0x300, "They Went That-a-Way", 5, "Find the man in the checkered coat"),
  };
  CHECK_EQ(BuildQuestListing(std::nullopt, rows),
           std::string("## Current quest objectives\n\n### Ain't That a Kick\n- `1` - Talk to Sunny - here\n"
                       "- Ask around about the Courier - no map marker, so it has no id to select or follow\n\n"
                       "### Back in the Saddle\n- `2` - Find Sunny - there\n\n### They Went That-a-Way\n"
                       "- Find the man in the checkered coat - no map marker, so it has no id to select or follow"));
}

TEST("quest listing: order is priority first, then quest, objective and target") {
  CHECK(ListedBefore({60, 9, 9, 9}, {50, 1, 1, 1}));
  CHECK(ListedBefore({50, 1, 9, 9}, {50, 2, 1, 1}));
  CHECK(ListedBefore({50, 1, 1, 9}, {50, 1, 2, 1}));
  CHECK(ListedBefore({50, 1, 1, 1}, {50, 1, 1, 2}));
  CHECK(!ListedBefore({50, 1, 1, 1}, {50, 1, 1, 1}));
}
