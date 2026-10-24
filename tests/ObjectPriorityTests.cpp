#include "Context/ObjectPriority.hpp"
#include "TestFramework.hpp"

using namespace Objects;

namespace {
Candidate At(uint32_t id, Category category, float distance, bool quest = false) {
  return {id, category, distance, quest};
}
} // namespace

TEST("objects: every listed category has a name, none has the empty one") {
  for (int value = static_cast<int>(Category::Actor); value <= static_cast<int>(Category::Loot); ++value)
    CHECK(std::string(CategoryName(static_cast<Category>(value))).size() > 2);
  CHECK(std::string(CategoryName(Category::None)).empty());
  CHECK_EQ(std::string(CategoryName(Category::Corpse)), std::string("corpse"));
  CHECK_EQ(std::string(CategoryName(Category::Harvestable)), std::string("harvestable"));
}

TEST("objects: loose items are items, built-in things are not") {
  CHECK(IsLooseItem(Category::Weapon) && IsLooseItem(Category::Loot) && IsLooseItem(Category::Readable));
  CHECK(!IsLooseItem(Category::Door) && !IsLooseItem(Category::Container) && !IsLooseItem(Category::Actor));
  CHECK(!IsLooseItem(Category::None));
}

TEST("objects: quest targets beat everything, people beat containers, clutter comes last") {
  std::vector<Candidate> items = {At(1, Category::Loot, 10), At(2, Category::Container, 900),
                                  At(3, Category::Actor, 800), At(4, Category::Loot, 3000, true),
                                  At(5, Category::Door, 50)};
  Rank(items, 10);
  CHECK(items[0].formId == 4); // a bottle that a quest points at
  CHECK(items[1].formId == 3);
  CHECK(items[2].formId == 2);
  CHECK(items[3].formId == 5);
  CHECK(items[4].formId == 1);
}

TEST("objects: inside one tier the closest comes first, then the lower form id, so ids are reproducible") {
  std::vector<Candidate> items = {At(9, Category::Container, 500), At(7, Category::Container, 100),
                                  At(8, Category::Container, 500), At(6, Category::Container, 300)};
  Rank(items, 10);
  CHECK(items[0].formId == 7 && items[1].formId == 6 && items[2].formId == 8 && items[3].formId == 9);
  auto again = std::vector<Candidate>{At(6, Category::Container, 300), At(8, Category::Container, 500),
                                      At(7, Category::Container, 100), At(9, Category::Container, 500)};
  Rank(again, 10);
  for (size_t i = 0; i < items.size(); ++i)
    CHECK(items[i].formId == again[i].formId);
}

TEST("objects: the cap keeps the most important and reports how many there were") {
  std::vector<Candidate> items;
  for (uint32_t i = 1; i <= 40; ++i)
    items.push_back(At(i, Category::Loot, static_cast<float>(i)));
  items.push_back(At(100, Category::Actor, 2400.0f));
  items.push_back(At(101, Category::Container, 2300.0f));
  CHECK(Rank(items, 20) == 42);
  CHECK(items.size() == 20);
  CHECK(items[0].formId == 100 && items[1].formId == 101);
  CHECK(items[2].formId == 1); // the closest junk fills the rest
}

TEST("objects: fewer than the cap keeps everything and says nothing") {
  std::vector<Candidate> items = {At(1, Category::Door, 10)};
  CHECK(Rank(items, 20) == 1);
  CHECK(TruncationNote(1, 1).empty());
  CHECK(TruncationNote(20, 20).empty());
}

TEST("objects: the truncation note names both numbers") {
  const std::string note = TruncationNote(20, 57);
  CHECK(note.find("20 of 57") != std::string::npos);
  CHECK(note.find("most important first") != std::string::npos);
}
