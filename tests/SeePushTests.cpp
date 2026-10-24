#include "Context/SeePush.hpp"
#include "TestFramework.hpp"

using Context::SeenObject;
using Context::SeePush;
using Objects::Category;

namespace {

SeenObject Thing(uint32_t id, Category category, const char *name, float distance = 700.0f, bool quest = false) {
  return {id, category, name, distance, quest};
}

} // namespace

TEST("see push: only quest targets, containers and living people are notable") {
  CHECK(Context::IsNotable(Category::Container, false));
  CHECK(Context::IsNotable(Category::Actor, false));
  CHECK(Context::IsNotable(Category::Loot, true)); // a quest bottle counts
  CHECK(!Context::IsNotable(Category::Loot, false));
  CHECK(!Context::IsNotable(Category::Corpse, false));
  CHECK(!Context::IsNotable(Category::Door, false));
  CHECK(!Context::IsNotable(Category::Terminal, false));
}

TEST("see push: a quiet cell sends nothing") {
  SeePush push;
  CHECK(!push.Update({Thing(1, Category::Loot, "Tin can"), Thing(2, Category::Door, "Door")}, 1.0f));
  CHECK(!push.Update({}, 100.0f));
}

TEST("see push: a container and a person produce one silent summary without ids") {
  SeePush push;
  auto message = push.Update(
      {Thing(1, Category::Container, "Footlocker", 560.0f), Thing(2, Category::Actor, "Easy Pete", 1400.0f)}, 1.0f);
  REQUIRE(message.has_value());
  CHECK(message->silent);
  CHECK(message->text.find("## You see") == 0);
  CHECK(message->text.find("Easy Pete (20 meters)") != std::string::npos);
  CHECK(message->text.find("Footlocker (8 meters)") != std::string::npos);
  CHECK(message->text.find('`') == std::string::npos);
  CHECK(message->text.find("query_nearby") != std::string::npos);
  // People come before containers, as in the importance tiers.
  CHECK(message->text.find("People") < message->text.find("Containers"));
}

TEST("see push: a quest target makes the push non-silent and leads it") {
  SeePush push;
  auto message = push.Update(
      {Thing(1, Category::Container, "Footlocker"), Thing(2, Category::Actor, "Sunny Smiles", 300.0f, true)}, 1.0f);
  REQUIRE(message.has_value());
  CHECK(!message->silent);
  CHECK(message->text.find("Quest target: Sunny Smiles") < message->text.find("Footlocker"));
}

TEST("see push: a form id is never reported twice in a space") {
  SeePush push;
  const std::vector<SeenObject> world = {Thing(1, Category::Container, "Footlocker")};
  REQUIRE(push.Update(world, 1.0f).has_value());
  for (int i = 0; i < 20; ++i)
    CHECK(!push.Update(world, 30.0f));
  push.Reset(); // a door: the cell is a new space
  CHECK(push.Update(world, 30.0f).has_value());
}

TEST("see push: pushes are at least the interval apart, and the waiting thing is reported once the interval is over") {
  SeePush push;
  REQUIRE(push.Update({Thing(1, Category::Container, "Footlocker")}, 1.0f).has_value());
  const std::vector<SeenObject> more = {Thing(1, Category::Container, "Footlocker"),
                                        Thing(2, Category::Actor, "Easy Pete")};
  CHECK(!push.Update(more, 5.0f));
  CHECK(!push.Update(more, 10.0f));
  auto later = push.Update(more, 6.0f);
  REQUIRE(later.has_value());
  CHECK(later->text.find("Easy Pete") != std::string::npos);
  CHECK(later->text.find("Footlocker") == std::string::npos); // already seen
}

TEST("see push: at most five entries, the most important first, the rest wait for the next push") {
  SeePush push;
  std::vector<SeenObject> world;
  for (uint32_t i = 1; i <= 8; ++i)
    world.push_back(Thing(i, Category::Container, ("Box " + std::to_string(i)).c_str(), 100.0f * i));
  auto first = push.Update(world, 1.0f);
  REQUIRE(first.has_value());
  CHECK(first->text.find("Box 5") != std::string::npos);
  CHECK(first->text.find("Box 6") == std::string::npos);
  auto second = push.Update(world, SeePush::kMinInterval);
  REQUIRE(second.has_value());
  CHECK(second->text.find("Box 6") != std::string::npos && second->text.find("Box 1") == std::string::npos);
}

TEST("see push: reset all starts over, and the first push after a load is not held back") {
  SeePush push;
  const std::vector<SeenObject> world = {Thing(1, Category::Actor, "Easy Pete")};
  REQUIRE(push.Update(world, 1.0f).has_value());
  push.ResetAll();
  CHECK(push.Update(world, 0.5f).has_value());
}
