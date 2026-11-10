#include "Context/QuestListing.hpp"
#include "TestFramework.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Quests;

namespace {

constexpr uint32_t kKick = 0x100;
constexpr uint32_t kSaddle = 0x200;

Row Target(int priority, uint32_t quest, const char *questName, uint32_t objective, const char *text, uint32_t target,
           int id, const char *location) {
  return {{1, priority, quest, objective, target}, questName, "", text, id, location};
}

Row Markerless(int priority, uint32_t quest, const char *questName, uint32_t objective, const char *text) {
  return {{1, priority, quest, objective, 0}, questName, "", text, 0, {}};
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
  const std::string line = "You already have quest Ain't That a Kick selected (current objective: Talk to Sunny). "
                           "You are not following it right now; do_current_quest follows it.";
  CHECK_EQ(SelectedQuestLine(selected), line);
  const std::vector<Row> rows{Target(50, kKick, "Ain't That a Kick", 10, "Talk to Sunny", 1, 1, "here"),
                              Target(40, kSaddle, "Back in the Saddle", 10, "Find Sunny", 2, 2, "there")};
  CHECK_EQ(BuildQuestListing(selected, rows),
           line + "\n\n## Current quest objectives\n\n### Ain't That a Kick (selected)\n- `1` - Talk to Sunny - here"
                  "\n\n### Back in the Saddle\n- `2` - Find Sunny - there");
}

TEST("quest listing: an unknown objective names only the quest") {
  CHECK_EQ(SelectedQuestLine(SelectedQuest{"Ghost Town Gunfight", "", 1}),
           std::string("You already have quest Ghost Town Gunfight selected. "
                       "You are not following it right now; do_current_quest follows it."));
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
  CHECK(ListedBefore({0, 10, 9, 9, 9}, {1, 90, 1, 1, 1})); // main story beats any priority
  CHECK(ListedBefore({1, 60, 9, 9, 9}, {1, 50, 1, 1, 1}));
  CHECK(ListedBefore({1, 50, 1, 9, 9}, {1, 50, 2, 1, 1}));
  CHECK(ListedBefore({1, 50, 1, 1, 9}, {1, 50, 1, 2, 1}));
  CHECK(ListedBefore({1, 50, 1, 1, 1}, {1, 50, 1, 1, 2}));
  CHECK(!ListedBefore({1, 50, 1, 1, 1}, {1, 50, 1, 1, 1}));
}

namespace {

constexpr std::string_view kMainIds[] = {"MainQuestOne", "MainQuestTwo"};

} // namespace

TEST("quest category: base game quests are untold apart while the main list is empty") {
  CHECK(MainQuestEditorIds().empty());
  const Category category = Classify("FalloutNV.esm", "MainQuestOne", MainQuestEditorIds());
  CHECK(category.kind == CategoryKind::BaseGame);
  CHECK_EQ(category.label, std::string("base game"));
  CHECK(Classify("", "Anything", MainQuestEditorIds()).kind == CategoryKind::BaseGame); // unknown file: base game
}

TEST("quest category: with a main list, base game quests are main story or side quests") {
  CHECK(Classify("FalloutNV.esm", "MainQuestOne", kMainIds).kind == CategoryKind::Main);
  CHECK(Classify("falloutnv.ESM", "mainquesttwo", kMainIds).kind == CategoryKind::Main); // case does not matter
  CHECK_EQ(Classify("FalloutNV.esm", "MainQuestOne", kMainIds).label, std::string("main story"));
  CHECK(Classify("FalloutNV.esm", "SomeSideQuest", kMainIds).kind == CategoryKind::Side);
  CHECK_EQ(Classify("FalloutNV.esm", "", kMainIds).label, std::string("side quest"));
}

TEST("quest category: DLC by plugin file, anything else is a mod named after its file") {
  const Category deadMoney = Classify("DeadMoney.esm", "MainQuestOne", kMainIds); // the main list is base game only
  CHECK(deadMoney.kind == CategoryKind::Dlc);
  CHECK_EQ(deadMoney.label, std::string("DLC: Dead Money"));
  CHECK_EQ(Classify("oldworldblues.esm", "", kMainIds).label, std::string("DLC: Old World Blues"));
  const Category mod = Classify("MyQuestMod.esp", "", kMainIds);
  CHECK(mod.kind == CategoryKind::Mod);
  CHECK_EQ(mod.label, std::string("mod: MyQuestMod"));
  CHECK_EQ(Classify("NoExtension", "", kMainIds).label, std::string("mod: NoExtension"));
}

TEST("quest category: main story ranks first, DLC, side and mods share the second rank") {
  CHECK_EQ(CategoryRank(CategoryKind::Main), 0);
  CHECK_EQ(CategoryRank(CategoryKind::Side), 1);
  CHECK_EQ(CategoryRank(CategoryKind::BaseGame), 1);
  CHECK_EQ(CategoryRank(CategoryKind::Dlc), 1);
  CHECK_EQ(CategoryRank(CategoryKind::Mod), 1);
}

TEST("quest listing: the heading carries the category tag, and selected after it") {
  const SelectedQuest selected{"Ain't That a Kick", "", kKick};
  std::vector<Row> rows{Target(50, kKick, "Ain't That a Kick", 10, "Talk to Sunny", 1, 1, "here"),
                        Target(60, kSaddle, "Bitter Springs", 10, "Find the camp", 2, 2, "there"),
                        Target(10, 0x300, "Side Thing", 10, "Do it", 3, 3, "far")};
  rows[0].key.rank = 0;
  rows[0].category = "main story";
  rows[1].category = "DLC: Honest Hearts";
  CHECK_EQ(BuildQuestListing(selected, rows),
           SelectedQuestLine(selected) +
               "\n\n## Current quest objectives\n\n### Ain't That a Kick (main story, selected)\n"
               "- `1` - Talk to Sunny - here\n\n### Bitter Springs (DLC: Honest Hearts)\n- `2` - Find the camp - there"
               "\n\n### Side Thing\n- `3` - Do it - far");
}

TEST("quest listing: the selected line says whether do_current_quest is following the quest") {
  SelectedQuest selected{"Ain't That a Kick", "Talk to Sunny", kKick};
  selected.routeRunning = true;
  CHECK_EQ(SelectedQuestLine(selected),
           std::string("You already have quest Ain't That a Kick selected (current objective: Talk to Sunny). "
                       "do_current_quest is following it."));
  selected.routeRunning = false;
  CHECK(SelectedQuestLine(selected).find("not following it right now") != std::string::npos);
}

namespace {

std::vector<Row> TwoObjectives() {
  return {Target(50, kKick, "Ain't That a Kick", 10, "Talk to Sunny", 1, 1, "here"),
          Target(50, kKick, "Ain't That a Kick", 20, "Kill the geckos", 5, 2, "north"),
          Target(50, kKick, "Ain't That a Kick", 20, "Kill the geckos", 6, 3, "south"),
          Target(40, kSaddle, "Back in the Saddle", 20, "Kill the geckos", 6, 4, "far")};
}

std::vector<bool> Marks(std::vector<Row> rows) {
  std::vector<bool> marks;
  for (const Row &row : rows)
    marks.push_back(row.selected);
  return marks;
}

} // namespace

TEST("quest listing: only the target select_quest chose is tagged (selected)") {
  SelectedQuest selected{"Ain't That a Kick", "Kill the geckos", kKick, Chosen{20, 6}};
  auto rows = TwoObjectives();
  MarkSelectedRows(selected, rows);
  CHECK(Marks(rows) == (std::vector<bool>{false, false, true, false}));
  CHECK_EQ(
      BuildQuestListing(selected, TwoObjectives()),
      SelectedQuestLine(selected) +
          "\n\n## Current quest objectives\n\n### Ain't That a Kick (selected)\n- `1` - Talk to Sunny - here\n"
          "- `2` - Kill the geckos (place 1 of 2) - north\n- `3` (selected) - Kill the geckos (place 2 of 2) - south"
          "\n\n### Back in the Saddle\n- `4` - Kill the geckos - far");
}

TEST("quest listing: a vanished target tags every target of the chosen objective") {
  auto rows = TwoObjectives();
  MarkSelectedRows(SelectedQuest{"Ain't That a Kick", "", kKick, Chosen{20, 0xFF}}, rows);
  CHECK(Marks(rows) == (std::vector<bool>{false, true, true, false}));
}

TEST("quest listing: nothing is tagged without a choice or a selected quest, markerless rows never") {
  auto rows = TwoObjectives();
  MarkSelectedRows(SelectedQuest{"Ain't That a Kick", "", kKick}, rows); // picked in the Pip-Boy
  CHECK(Marks(rows) == (std::vector<bool>{false, false, false, false}));
  MarkSelectedRows(std::nullopt, rows);
  CHECK(Marks(rows) == (std::vector<bool>{false, false, false, false}));

  std::vector<Row> markerless{Markerless(50, kKick, "Ain't That a Kick", 30, "Ask around")};
  MarkSelectedRows(SelectedQuest{"Ain't That a Kick", "", kKick, Chosen{30, 0}}, markerless);
  CHECK(!markerless[0].selected);
}
