#include "Context/QuestListing.hpp"
#include "TestFramework.hpp"
#include <optional>
#include <string>
#include <vector>

using namespace Quests;

TEST("quest listing: nothing selected adds no line and keeps the list unchanged") {
  CHECK(SelectedQuestLine(std::nullopt).empty());
  CHECK_EQ(BuildQuestListing(std::nullopt, {"`1` - Ain't That a Kick: Talk to Sunny - 12 m away"}),
           std::string("## Current quest objectives\n- `1` - Ain't That a Kick: Talk to Sunny - 12 m away"));
  CHECK_EQ(BuildQuestListing(std::nullopt, {}), std::string("## Current quest objectives\n- None."));
}

TEST("quest listing: a selected quest leads the result, above the unchanged list") {
  const SelectedQuest selected{"Ain't That a Kick", "Talk to Sunny"};
  const std::vector<std::string> lines{"`1` - Ain't That a Kick: Talk to Sunny - 12 m away",
                                       "`2` - Back in the Saddle: Find Sunny - 40 m away"};
  const std::string line = "You already have quest Ain't That a Kick selected (current objective: Talk to Sunny).";
  CHECK_EQ(SelectedQuestLine(selected), line);
  CHECK_EQ(BuildQuestListing(selected, lines), line + "\n\n" + BuildQuestListing(std::nullopt, lines));
}

TEST("quest listing: an unknown objective names only the quest") {
  const SelectedQuest selected{"Ghost Town Gunfight", ""};
  CHECK_EQ(SelectedQuestLine(selected), std::string("You already have quest Ghost Town Gunfight selected."));
}

TEST("quest listing: the selected line also appears when no objective is listed") {
  const SelectedQuest selected{"Ghost Town Gunfight", ""};
  CHECK_EQ(BuildQuestListing(selected, {}), SelectedQuestLine(selected) + "\n\n## Current quest objectives\n- None.");
}
