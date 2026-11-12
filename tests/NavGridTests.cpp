#include "BoxWorld.hpp"
#include "Movement/NavGrid.hpp"
#include "TestFramework.hpp"
#include <cmath>
#include <limits>
#include <vector>

using namespace NavGrid;
using TestWorld::BoxWorld;

namespace {

const PathProbe::Settings kProbe = [] {
  PathProbe::Settings settings;
  settings.halfWidth = 20.3f; // the player's collision radius, as the engine side supplies it
  return settings;
}();
constexpr int kNoBudget = std::numeric_limits<int>::max();

bool Near(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

float Distance2D(const Point3 &a, const Point3 &b) { return std::hypot(a.x - b.x, a.y - b.y); }

Search Run(BoxWorld &world, Grid &grid, const Point3 &start, const Point3 &goal, const SearchSettings &settings = {}) {
  Search search;
  search.Start(grid, start, goal, settings);
  search.Step(world, kProbe, kNoBudget);
  return search;
}

// Every leg of the route, from the start on, can be walked at the player's width.
bool Walkable(BoxWorld &world, const Point3 &start, const std::vector<Point3> &path) {
  Point3 from = start;
  for (const Point3 &to : path) {
    const PathProbe::SweepResult leg = PathProbe::Sweep(world, from, to, 1.0e9f, kProbe, kProbe.halfWidth);
    if (!leg.Clear())
      return false;
    from = leg.end;
  }
  return true;
}

} // namespace

TEST("nav grid: cells are aligned to the world, also below zero, and levels tell floors apart") {
  const Cell cell = CellAt({-1.0f, 33.0f, 200.0f});
  CHECK_EQ(cell.x, -1);
  CHECK_EQ(cell.y, 1);
  CHECK_EQ(cell.level, 3);
  const Point3 centre = CentreOf(cell, 200.0f);
  CHECK(Near(centre.x, -16.0f, 0.01f));
  CHECK(Near(centre.y, 48.0f, 0.01f));
  CHECK(CellAt(centre) == cell);
  CHECK(KeyOf(cell) != KeyOf({-1, 1, 2}));
  CHECK(KeyOf(cell) != KeyOf({1, -1, 3}));
  CHECK(KeyOf({-60000, -50000, -3}) != KeyOf({-60000, -50000, 3}));
}

TEST("nav grid: open ground gives a direct route") {
  BoxWorld world;
  Grid grid;
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Point3 goal = {600.0f, 16.0f, 0.0f};
  const Search search = Run(world, grid, start, goal);
  REQUIRE(search.GetState() == Search::State::Found);
  CHECK(Distance2D(search.Path().back(), goal) <= 48.0f);
  CHECK(search.Path().size() <= 19u); // about one cell per 32 units, nothing wasted
  CHECK(Walkable(world, start, search.Path()));
  CHECK_EQ(MergeStraight(search.Path()).size(), 2u); // one straight run: its two ends
}

TEST("nav grid: a car across the way is walked around") {
  BoxWorld world;
  world.Add({200.0f, -150.0f, 0.0f}, {340.0f, 150.0f, 100.0f});
  Grid grid;
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Point3 goal = {700.0f, 16.0f, 0.0f};
  const Search search = Run(world, grid, start, goal);
  REQUIRE(search.GetState() == Search::State::Found);
  CHECK(Walkable(world, start, search.Path()));
  float furthestAside = 0.0f;
  for (const Point3 &point : search.Path())
    furthestAside = std::max(furthestAside, std::abs(point.y));
  CHECK(furthestAside > 150.0f);
  CHECK(Walkable(world, start, MergeStraight(search.Path())));
  CHECK(MergeStraight(search.Path()).size() < search.Path().size());
}

TEST("nav grid: a pocket is backed out of") {
  BoxWorld world;
  // A U open towards the start, with the goal straight behind its closed end.
  world.Add({400.0f, -200.0f, 0.0f}, {440.0f, 200.0f, 150.0f});
  world.Add({100.0f, 200.0f, 0.0f}, {440.0f, 240.0f, 150.0f});
  world.Add({100.0f, -240.0f, 0.0f}, {440.0f, -200.0f, 150.0f});
  Grid grid;
  const Point3 start = {300.0f, 16.0f, 0.0f};
  const Point3 goal = {700.0f, 16.0f, 0.0f};
  const Search search = Run(world, grid, start, goal);
  REQUIRE(search.GetState() == Search::State::Found);
  CHECK(Walkable(world, start, search.Path()));
}

TEST("nav grid: walled in means no route") {
  BoxWorld world;
  world.Add({-200.0f, -200.0f, 0.0f}, {200.0f, -160.0f, 150.0f});
  world.Add({-200.0f, 160.0f, 0.0f}, {200.0f, 200.0f, 150.0f});
  world.Add({-200.0f, -200.0f, 0.0f}, {-160.0f, 200.0f, 150.0f});
  world.Add({160.0f, -200.0f, 0.0f}, {200.0f, 200.0f, 150.0f});
  Grid grid;
  const Search search = Run(world, grid, {16.0f, 16.0f, 0.0f}, {900.0f, 16.0f, 0.0f});
  // The cell nearest the goal inside the walls is a step nearer, but not the goal.
  CHECK(search.GetState() != Search::State::Found);
  if (search.GetState() == Search::State::Partial)
    CHECK(search.Path().back().x < 160.0f);
}

TEST("nav grid: a gap narrower than the player is not a way through") {
  BoxWorld world;
  world.Add({200.0f, 15.0f, 0.0f}, {260.0f, 3000.0f, 150.0f});
  world.Add({200.0f, -3000.0f, 0.0f}, {260.0f, -15.0f, 150.0f});
  Grid grid;
  SearchSettings settings;
  settings.maxExpansions = 600;
  const Search search = Run(world, grid, {16.0f, 16.0f, 0.0f}, {600.0f, 16.0f, 0.0f}, settings);
  CHECK(search.GetState() != Search::State::Found);
}

TEST("nav grid: a ray budget cuts the search into calls with the same route") {
  BoxWorld world;
  world.Add({200.0f, -150.0f, 0.0f}, {340.0f, 150.0f, 100.0f});
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Point3 goal = {700.0f, 16.0f, 0.0f};
  Grid whole;
  const Search atOnce = Run(world, whole, start, goal);

  Grid grid;
  Search search;
  search.Start(grid, start, goal);
  int calls = 0;
  while (search.GetState() == Search::State::Working && calls < 100000) {
    const int before = search.Rays();
    search.Step(world, kProbe, 300);
    ++calls;
    CHECK(search.Rays() - before <= 300 + 120); // the budget plus the edges of the cell that crossed it
  }
  CHECK(calls > 3);
  REQUIRE(search.GetState() == Search::State::Found);
  REQUIRE(search.Path().size() == atOnce.Path().size());
  for (size_t index = 0; index < search.Path().size(); ++index)
    CHECK(Distance2D(search.Path()[index], atOnce.Path()[index]) < 0.01f);
  CHECK_EQ(search.Rays(), atOnce.Rays());
}

TEST("nav grid: what was sampled is remembered, so the same search again casts no rays") {
  BoxWorld world;
  world.Add({200.0f, -150.0f, 0.0f}, {340.0f, 150.0f, 100.0f});
  Grid grid;
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Point3 goal = {700.0f, 16.0f, 0.0f};
  const Search first = Run(world, grid, start, goal);
  REQUIRE(first.GetState() == Search::State::Found);
  CHECK(first.Rays() > 0);
  CHECK(grid.NodeCount() > 0u);
  const Search second = Run(world, grid, start, goal);
  CHECK(second.GetState() == Search::State::Found);
  CHECK_EQ(second.Rays(), 0);
  CHECK_EQ(second.Path().size(), first.Path().size());
}

TEST("nav grid: stairs are climbed, and a deck overhead is another level") {
  BoxWorld stairs;
  for (int step = 0; step < 10; ++step) {
    const float x = 200.0f + 20.0f * static_cast<float>(step);
    stairs.Add({x, -200.0f, 0.0f}, {x + 20.0f, 200.0f, 18.0f * static_cast<float>(step + 1)});
  }
  stairs.Add({400.0f, -200.0f, 0.0f}, {1200.0f, 200.0f, 180.0f});
  Grid grid;
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Search up = Run(stairs, grid, start, {800.0f, 16.0f, 180.0f});
  REQUIRE(up.GetState() == Search::State::Found);
  CHECK(Near(up.Path().back().z, 180.0f, 0.5f));

  // A deck 200 units up with nothing leading onto it: the way underneath stays open, the deck is out of reach.
  BoxWorld bridge;
  bridge.Add({200.0f, -400.0f, 200.0f}, {400.0f, 400.0f, 210.0f});
  Grid under;
  const Search below = Run(bridge, under, start, {800.0f, 16.0f, 0.0f});
  REQUIRE(below.GetState() == Search::State::Found);
  for (const Point3 &point : below.Path())
    CHECK(Near(point.z, 0.0f, 0.5f));
  SearchSettings small;
  small.maxExpansions = 800;
  const Search onto = Run(bridge, under, start, {300.0f, 16.0f, 210.0f}, small);
  CHECK(onto.GetState() != Search::State::Found);
}

TEST("nav grid: out of patience, the route leads to the nearest cell reached") {
  BoxWorld world;
  Grid grid;
  SearchSettings settings;
  settings.maxExpansions = 20;
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Point3 goal = {5000.0f, 16.0f, 0.0f};
  const Search search = Run(world, grid, start, goal, settings);
  REQUIRE(search.GetState() == Search::State::Partial);
  CHECK(search.Path().back().x > 300.0f);
  CHECK_EQ(search.Expansions(), 20);
  // Not a step nearer than the start: nothing worth walking.
  settings.maxExpansions = 2;
  Grid fresh;
  CHECK(Run(world, fresh, start, goal, settings).GetState() == Search::State::Failed);
}

TEST("nav grid: a door is asked again by every search, a wall is not") {
  BoxWorld world;
  world.Add({200.0f, -3000.0f, 0.0f}, {240.0f, -50.0f, 150.0f});
  world.Add({200.0f, 50.0f, 0.0f}, {240.0f, 3000.0f, 150.0f});
  world.Add({205.0f, -50.0f, 0.0f}, {235.0f, 50.0f, 150.0f}, PathProbe::HitKind::Door, 0xD0);
  Grid grid;
  SearchSettings settings;
  settings.maxExpansions = 600;
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Point3 goal = {600.0f, 16.0f, 0.0f};
  CHECK(Run(world, grid, start, goal, settings).GetState() != Search::State::Found);
  world.boxes.pop_back(); // the door opens
  const Search open = Run(world, grid, start, goal, settings);
  CHECK(open.GetState() == Search::State::Found);
  CHECK(open.Rays() > 0);
  // The near side was not sampled again: a search on a grid that knows nothing costs more.
  Grid fresh;
  CHECK(open.Rays() < Run(world, fresh, start, goal, settings).Rays());
}

TEST("nav grid: where she got stuck is blocked from then on") {
  BoxWorld world;
  Grid grid;
  const Point3 start = {16.0f, 16.0f, 0.0f};
  const Point3 goal = {600.0f, 16.0f, 0.0f};
  const Search direct = Run(world, grid, start, goal);
  REQUIRE(direct.GetState() == Search::State::Found);
  CHECK(Near(direct.Path().front().y, 16.0f, 0.5f));
  // Something the rays do not see stops her at the third cell.
  grid.Block({80.0f, 16.0f, 0.0f}, 1.0f, 0.0f);
  const Search around = Run(world, grid, start, goal);
  REQUIRE(around.GetState() == Search::State::Found);
  bool leftTheLine = false;
  for (const Point3 &point : around.Path())
    leftTheLine = leftTheLine || std::abs(point.y - 16.0f) > 1.0f;
  CHECK(leftTheLine);
}

TEST("nav grid: straight runs are merged, corners are kept") {
  const std::vector<Point3> path = {{0.0f, 0.0f, 0.0f},   {32.0f, 0.0f, 0.0f},   {64.0f, 0.0f, 5.0f},
                                    {96.0f, 32.0f, 5.0f}, {128.0f, 64.0f, 5.0f}, {128.0f, 96.0f, 5.0f}};
  const auto merged = MergeStraight(path);
  REQUIRE(merged.size() == 4u);
  CHECK(Near(merged[1].x, 64.0f, 0.01f));
  CHECK(Near(merged[2].x, 128.0f, 0.01f));
  CHECK(Near(merged[2].y, 64.0f, 0.01f));
  CHECK_EQ(MergeStraight({{0.0f, 0.0f, 0.0f}, {32.0f, 0.0f, 0.0f}}).size(), 2u);
}

TEST("nav grid: a wall whose face lies exactly on a line of cell centres is still a wall") {
  // The cell centres along the route are at x = 16 + 32 n; one of them is exactly 400, the wall's near face.
  BoxWorld world;
  world.Add({400.0f, -3000.0f, 0.0f}, {440.0f, 3000.0f, 150.0f});
  Grid grid;
  SearchSettings settings;
  settings.maxExpansions = 800;
  const Search search = Run(world, grid, {304.0f, 16.0f, 0.0f}, {700.0f, 16.0f, 0.0f}, settings);
  CHECK(search.GetState() != Search::State::Found);
  for (const Point3 &point : search.Path())
    CHECK(point.x < 400.0f - 20.0f); // and no cell nearer the wall than the player's radius
}
