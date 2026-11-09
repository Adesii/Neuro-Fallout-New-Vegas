#include "Context/QuestListing.hpp"
#include "TestFramework.hpp"
#include <string>
#include <vector>

using namespace QuestListing;

namespace {

const std::vector<Candidate> kCandidates = {{0x10, 1, 0xA}, {0x10, 2, 0xB}, {0x10, 2, 0xC}, {0x20, 1, 0xD}};

} // namespace

TEST("quest listing: no selected quest adds no line and the list is unchanged") {
  const std::string text = Format({{1, "Quest A: Go", "in your current area, 5 m away", false}}, std::nullopt);
  CHECK_EQ(text, std::string("## Current quest objectives\n- `1` - Quest A: Go - in your current area, 5 m away"));
  CHECK_EQ(Format({}, std::nullopt), std::string("## Current quest objectives\n- None."));
}

TEST("quest listing: the selected quest is named first and the route state is said") {
  const std::string running = Format({{1, "Quest A: Go", "here", true}}, ActiveQuest{"Quest A", true});
  CHECK(running.find("\nYou already have quest Quest A selected. do_current_quest is following it.\n") !=
        std::string::npos);
  CHECK(running.find("- `1` (selected) - Quest A: Go - here") != std::string::npos);
  const std::string idle = Format({}, ActiveQuest{"Quest A", false});
  CHECK(idle.find("not following it right now") != std::string::npos);
  CHECK(idle.find("- None.") != std::string::npos);
}

TEST("quest listing: only the chosen target is tagged") {
  const auto marks = MarkSelected(kCandidates, 0x10, Chosen{0x10, 2, 0xC});
  CHECK(!marks[0]);
  CHECK(!marks[1]);
  CHECK(marks[2]);
  CHECK(!marks[3]);
}

TEST("quest listing: a vanished target tags every target of the chosen objective") {
  const auto marks = MarkSelected(kCandidates, 0x10, Chosen{0x10, 2, 0xFF});
  CHECK(!marks[0]);
  CHECK(marks[1]);
  CHECK(marks[2]);
  CHECK(!marks[3]);
}

TEST("quest listing: nothing is tagged without an active quest or for another quest's choice") {
  for (bool mark : MarkSelected(kCandidates, 0, Chosen{0x10, 2, 0xC}))
    CHECK(!mark);
  for (bool mark : MarkSelected(kCandidates, 0x20, Chosen{0x10, 2, 0xC}))
    CHECK(!mark);
  for (bool mark : MarkSelected(kCandidates, 0x10, std::nullopt))
    CHECK(!mark);
}
