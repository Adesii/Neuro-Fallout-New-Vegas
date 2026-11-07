#include "Context/NearbyListing.hpp"
#include "TestFramework.hpp"
#include <string>
#include <vector>

using namespace Nearby;

namespace {

std::vector<Entry> Entries(size_t count) {
  std::vector<Entry> entries;
  for (size_t index = 0; index < count; ++index)
    entries.push_back({"Thing " + std::to_string(index + 1), "loot"});
  return entries;
}

} // namespace

TEST("nearby query: no data or an empty object is page 1 without a search") {
  std::string error;
  auto none = ParseQuery("", error);
  REQUIRE(none.has_value());
  CHECK_EQ(none->page, 1);
  CHECK(none->search.empty());
  auto empty = ParseQuery("{}", error);
  REQUIRE(empty.has_value());
  CHECK_EQ(empty->page, 1);
}

TEST("nearby query: page and search are read") {
  std::string error;
  auto query = ParseQuery(R"({"page": 2, "search": "mail box"})", error);
  REQUIRE(query.has_value());
  CHECK_EQ(query->page, 2);
  CHECK_EQ(query->search, std::string("mail box"));
}

TEST("nearby query: bad input is refused with a reason") {
  std::string error;
  CHECK(!ParseQuery(R"({"page": 0})", error).has_value());
  CHECK(error.find("page") != std::string::npos);
  CHECK(!ParseQuery(R"({"page": "2"})", error).has_value());
  CHECK(!ParseQuery(R"({"search": 5})", error).has_value());
  CHECK(error.find("search") != std::string::npos);
  CHECK(!ParseQuery(R"({"search": "  !? "})", error).has_value());
  CHECK(!ParseQuery(R"({"search": ")" + std::string(70, 'a') + R"("})", error).has_value());
  CHECK(!ParseQuery(R"({"page": 1, "radius": 5})", error).has_value());
  CHECK(error.find("Only page and search") != std::string::npos);
  CHECK(!ParseQuery(R"({"page": 1, "page": 2})", error).has_value());
  CHECK(!ParseQuery("[1]", error).has_value());
  CHECK(!ParseQuery("not json", error).has_value());
}

TEST("nearby query: the schema advertises both optional fields") {
  const std::string schema = BuildQuerySchema().Serialize();
  CHECK(schema.find("\"page\"") != std::string::npos);
  CHECK(schema.find("\"search\"") != std::string::npos);
  CHECK(schema.find("\"required\"") == std::string::npos);
}

TEST("nearby listing: pages split the ranked list in twenties") {
  const auto selected = Select(Entries(38), "");
  REQUIRE(selected.size() == 38);
  CHECK_EQ(PageCount(38), size_t{2});
  CHECK_EQ(PageCount(0), size_t{1});
  CHECK_EQ(PageCount(20), size_t{1});
  const auto first = PageSlice(selected, 1);
  const auto second = PageSlice(selected, 2);
  REQUIRE(first.size() == 20 && second.size() == 18);
  CHECK_EQ(first.front(), size_t{0});
  CHECK_EQ(second.front(), size_t{20});
  CHECK_EQ(second.back(), size_t{37});
  CHECK(PageSlice(selected, 3).empty());
  CHECK(PageSlice(selected, 0).empty());
}

TEST("nearby listing: a search keeps only matches, best first, importance within the same match") {
  const std::vector<Entry> ranked = {
      {"Sunny Smiles", "actor"},  {"Crate", "container"},      {"Mailbox", "container"},
      {"Metal Box", "container"}, {"Ammo Crate", "container"}, {"Door", "door"},
  };
  const auto crates = Select(ranked, "crate");
  REQUIRE(crates.size() == 2);
  CHECK_EQ(crates[0], size_t{1}); // exact name
  CHECK_EQ(crates[1], size_t{4});
  const auto boxes = Select(ranked, "box");
  REQUIRE(boxes.size() == 2);
  CHECK_EQ(boxes[0], size_t{3}); // the word "box" beats "box" inside "mailbox"
  CHECK_EQ(boxes[1], size_t{2});
  const auto containers = Select(ranked, "container");
  CHECK_EQ(containers.size(), size_t{4});
  CHECK(Select(ranked, "deathclaw").empty());
}

TEST("nearby listing: the note says the range and page, and nothing when it all fits") {
  CHECK(ListingNote(1, 12, "").empty());
  CHECK(ListingNote(1, 20, "").empty());
  const std::string first = ListingNote(1, 38, "");
  CHECK(first.find("Showing 1-20 of 38 (page 1 of 2)") != std::string::npos);
  CHECK(first.find("most important first") != std::string::npos);
  CHECK(ListingNote(2, 38, "").find("Showing 21-38 of 38 (page 2 of 2)") != std::string::npos);
  CHECK(first.find("query again") == std::string::npos); // state only, no instructions
}

TEST("nearby listing: search notes name the search and the match count") {
  CHECK(ListingNote(1, 1, "box").find("Search \"box\": 1 match") != std::string::npos);
  const std::string many = ListingNote(2, 25, "crate");
  CHECK(many.find("25 matches") != std::string::npos);
  CHECK(many.find("Showing 21-25 of 25 (page 2 of 2)") != std::string::npos);
  CHECK(NoMatchNote("xyz").find("\"xyz\"") != std::string::npos);
}

TEST("nearby listing: a missing page says how many pages there are") {
  CHECK(MissingPageNote(3, 38, "").find("Page 3 does not exist: 38 objects fill 2 pages.") != std::string::npos);
  CHECK(MissingPageNote(2, 1, "box").find("1 match for \"box\" fill 1 page.") != std::string::npos);
}
