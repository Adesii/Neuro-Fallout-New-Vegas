#include "BoxWorld.hpp"
#include "Movement/PathProbe.hpp"
#include "TestFramework.hpp"
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

using namespace PathProbe;
using TestWorld::BoxWorld;

namespace {

// A wrecked car lying across the line y = 0: its near face at x = `face`, 140 deep, between y = `right` and `left`.
void AddCar(BoxWorld &world, float face, float right, float left) {
  world.Add({face, right, 0.0f}, {face + 140.0f, left, 100.0f}, HitKind::World, 0xCA5);
}

// The half width is the player's collision radius as read in game; the engine side supplies it, so the tests do too.
const Settings kSettings = [] {
  Settings settings;
  settings.halfWidth = 20.3f;
  return settings;
}();
const Point3 kOrigin = {0.0f, 0.0f, 0.0f};

bool Near(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

// Runs a planner to the end with no ray budget to speak of.
DetourPlanner Plan(BoxWorld &world, const std::vector<Point3> &path, int preferredSide = 0) {
  const SweepResult blocked = SweepPath(world, path, 400.0f, kSettings, 0.0f);
  DetourPlanner planner;
  planner.Start(path, blocked, preferredSide);
  planner.Step(world, path.front(), kSettings, std::numeric_limits<int>::max());
  return planner;
}

} // namespace

TEST("path probe: flat open ground is clear for the whole sweep") {
  BoxWorld world;
  const SweepResult sweep = Sweep(world, kOrigin, {500.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f);
  CHECK(sweep.Clear());
  CHECK(Near(sweep.clearDistance, 400.0f, 0.5f));
  CHECK(Near(sweep.end.x, 400.0f, 0.5f));
  // 20 floor samples and 7 spans of two centre rays.
  CHECK_EQ(sweep.rays, 20 + 7 * 2);
  CHECK_EQ(world.casts, sweep.rays);
}

TEST("path probe: a car across the line blocks it at its near face") {
  BoxWorld world;
  AddCar(world, 200.0f, -150.0f, 150.0f);
  Trace trace;
  const SweepResult sweep = Sweep(world, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f, &trace);
  CHECK(sweep.blocker == Blocker::Obstacle);
  CHECK(Near(sweep.clearDistance, 200.0f, 1.0f));
  CHECK(Near(sweep.blockPoint.x, 200.0f, 1.0f));
  CHECK(Near(sweep.blockPoint.z, 0.0f, 0.5f)); // a ground point, not at ray height
  CHECK_EQ(sweep.hit.formId, 0xCA5u);
  CHECK(sweep.dirX > 0.99f);
  CHECK(!trace.rays.empty());
  CHECK(trace.rays.back().blocked);
}

TEST("path probe: something low enough to step over does not block") {
  BoxWorld world;
  world.Add({200.0f, -50.0f, 0.0f}, {240.0f, 50.0f, 20.0f}); // a kerb
  CHECK(Sweep(world, kOrigin, {500.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f).Clear());
}

TEST("path probe: a crate too high to step onto blocks, whichever ray finds it") {
  BoxWorld world;
  world.Add({200.0f, -50.0f, 0.0f}, {260.0f, 50.0f, 50.0f});
  const SweepResult sweep = Sweep(world, kOrigin, {500.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f);
  CHECK(!sweep.Clear());
  CHECK(sweep.blocker == Blocker::Rise || sweep.blocker == Blocker::Obstacle);
  if (sweep.blocker == Blocker::Rise)
    CHECK(Near(sweep.step, 50.0f, 0.5f)); // how high the step was, for the log
  CHECK(sweep.clearDistance <= 200.5f);
  CHECK(sweep.clearDistance >= 150.0f);
}

TEST("path probe: stairs and what is at their top are followed, not read as a wall") {
  BoxWorld world;
  // Ten steps, 20 deep and 18 high, then a landing at 180.
  for (int step = 0; step < 10; ++step) {
    const float x = 100.0f + 20.0f * static_cast<float>(step);
    world.Add({x, -100.0f, 0.0f}, {x + 20.0f, 100.0f, 18.0f * static_cast<float>(step + 1)});
  }
  world.Add({300.0f, -100.0f, 0.0f}, {800.0f, 100.0f, 180.0f});
  const SweepResult sweep = Sweep(world, kOrigin, {600.0f, 0.0f, 0.0f}, 500.0f, kSettings, 0.0f);
  CHECK(sweep.Clear());
  CHECK(Near(sweep.end.z, 180.0f, 0.5f));
  // The same landing without the stairs is a wall.
  BoxWorld wall;
  wall.Add({300.0f, -100.0f, 0.0f}, {800.0f, 100.0f, 180.0f});
  CHECK(!Sweep(wall, kOrigin, {600.0f, 0.0f, 0.0f}, 500.0f, kSettings, 0.0f).Clear());
}

TEST("path probe: stairs down are followed too, a ledge is a drop") {
  BoxWorld stairs(100000.0f);
  stairs.boxes.clear();
  stairs.Add({-1000.0f, -1000.0f, -1000.0f}, {100.0f, 1000.0f, 0.0f});
  for (int step = 0; step < 10; ++step) {
    const float x = 100.0f + 20.0f * static_cast<float>(step);
    stairs.Add({x, -1000.0f, -1000.0f}, {x + 20.0f, 1000.0f, -18.0f * static_cast<float>(step + 1)});
  }
  stairs.Add({300.0f, -1000.0f, -1000.0f}, {2000.0f, 1000.0f, -180.0f});
  const SweepResult down = Sweep(stairs, kOrigin, {600.0f, 0.0f, 0.0f}, 500.0f, kSettings, 0.0f);
  CHECK(down.Clear());
  CHECK(Near(down.end.z, -180.0f, 0.5f));

  BoxWorld ledge(100000.0f);
  ledge.boxes.clear();
  ledge.Add({-1000.0f, -1000.0f, -1000.0f}, {200.0f, 1000.0f, 0.0f});
  ledge.Add({200.0f, -1000.0f, -1000.0f}, {2000.0f, 1000.0f, -100.0f});
  const SweepResult edge = Sweep(ledge, kOrigin, {600.0f, 0.0f, 0.0f}, 500.0f, kSettings, 0.0f);
  CHECK(edge.blocker == Blocker::Drop);
  CHECK(Near(edge.clearDistance, 200.0f, 0.5f));
  CHECK(Near(edge.step, -100.0f, 0.5f));
}

TEST("path probe: no floor at all is a drop") {
  BoxWorld world(300.0f);
  const SweepResult sweep = Sweep(world, kOrigin, {600.0f, 0.0f, 0.0f}, 500.0f, kSettings, 0.0f);
  CHECK(sweep.blocker == Blocker::Drop);
  CHECK(Near(sweep.clearDistance, 300.0f, 0.5f));
}

TEST("path probe: actors never block, doors do as doors, the target ends the sweep as clear") {
  BoxWorld actor;
  actor.Add({200.0f, -20.0f, 0.0f}, {240.0f, 20.0f, 120.0f}, HitKind::Actor, 0xA1);
  CHECK(Sweep(actor, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f).Clear());

  BoxWorld door;
  door.Add({200.0f, -60.0f, 0.0f}, {210.0f, 60.0f, 140.0f}, HitKind::Door, 0xD0);
  const SweepResult atDoor = Sweep(door, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f);
  CHECK(atDoor.blocker == Blocker::Door);
  CHECK_EQ(atDoor.hit.formId, 0xD0u);

  BoxWorld target;
  target.Add({200.0f, -60.0f, 0.0f}, {300.0f, 60.0f, 140.0f}, HitKind::Target, 0x7A);
  const SweepResult atTarget = Sweep(target, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f);
  CHECK(atTarget.Clear());
  CHECK(atTarget.reachedTarget);
  CHECK(atTarget.clearDistance <= 200.5f);
}

TEST("path probe: side rays find a gap narrower than the player, and say which side is blocked") {
  BoxWorld gap;
  gap.Add({200.0f, 15.0f, 0.0f}, {260.0f, 300.0f, 120.0f});
  gap.Add({200.0f, -300.0f, 0.0f}, {260.0f, -15.0f, 120.0f});
  CHECK(Sweep(gap, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f).Clear());
  const SweepResult wide = Sweep(gap, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, kSettings.halfWidth);
  CHECK(wide.blocker == Blocker::Obstacle);
  CHECK_EQ(wide.blockedSide, 0);

  BoxWorld left;
  left.Add({200.0f, 15.0f, 0.0f}, {260.0f, 300.0f, 120.0f}); // +y is left of travelling along +x
  const SweepResult leftOnly = Sweep(left, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, kSettings.halfWidth);
  CHECK(leftOnly.blocker == Blocker::Obstacle);
  CHECK_EQ(leftOnly.blockedSide, 1);
  // Six rays per span instead of two.
  BoxWorld open;
  CHECK_EQ(Sweep(open, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, kSettings.halfWidth).rays, 20 + 7 * 6);

  // A post on the centre line, thinner than the gap between the side rays, still blocks.
  BoxWorld post;
  post.Add({200.0f, -5.0f, 0.0f}, {210.0f, 5.0f, 200.0f});
  const SweepResult atPost = Sweep(post, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, kSettings.halfWidth);
  CHECK(atPost.blocker == Blocker::Obstacle);
  CHECK_EQ(atPost.blockedSide, 0);
}

TEST("path probe: a polyline is swept leg by leg up to the look-ahead") {
  BoxWorld world;
  world.Add({150.0f, 200.0f, 0.0f}, {250.0f, 260.0f, 120.0f}); // on the second leg, 200 along it
  const std::vector<Point3> path = {kOrigin, {200.0f, 0.0f, 0.0f}, {200.0f, 600.0f, 0.0f}};
  const SweepResult blocked = SweepPath(world, path, 600.0f, kSettings, 0.0f);
  CHECK(blocked.blocker == Blocker::Obstacle);
  CHECK(Near(blocked.clearDistance, 400.0f, 1.0f));
  CHECK(blocked.dirY > 0.99f); // the direction of the leg it is blocked on
  CHECK(Near(blocked.blockPoint.y, 200.0f, 1.0f));
  // Not far enough ahead to see it.
  const SweepResult near = SweepPath(world, path, 300.0f, kSettings, 0.0f);
  CHECK(near.Clear());
  CHECK(Near(near.clearDistance, 300.0f, 0.5f));
}

TEST("path probe: polyline length, point along it and trimming its end") {
  const std::vector<Point3> path = {kOrigin, {100.0f, 0.0f, 0.0f}, {100.0f, 200.0f, 40.0f}};
  CHECK(Near(PathLength(path), 300.0f, 0.01f));
  CHECK(Near(PointAlong(path, 50.0f).x, 50.0f, 0.01f));
  const Point3 onSecond = PointAlong(path, 200.0f);
  CHECK(Near(onSecond.y, 100.0f, 0.01f));
  CHECK(Near(onSecond.z, 20.0f, 0.01f));
  CHECK(Near(PointAlong(path, 5000.0f).y, 200.0f, 0.01f));
  CHECK(Near(PointAlong(path, -5.0f).x, 0.0f, 0.01f));

  const auto trimmed = TrimEnd(path, 50.0f);
  CHECK_EQ(trimmed.size(), 3u);
  CHECK(Near(trimmed.back().y, 150.0f, 0.01f));
  const auto shorter = TrimEnd(path, 250.0f); // the whole second leg and half the first
  CHECK_EQ(shorter.size(), 2u);
  CHECK(Near(shorter.back().x, 50.0f, 0.01f));
  CHECK_EQ(TrimEnd(path, 5000.0f).size(), 1u);
}

TEST("path probe: candidates lie beside the blocked spot, nearest first, the preferred side ahead") {
  const auto plain = MakeCandidates({200.0f, 0.0f, 0.0f}, 1.0f, 0.0f, 0, 0);
  CHECK_EQ(plain.size(), 12u);
  CHECK_EQ(plain[0].side, 1);
  CHECK_EQ(plain[1].side, -1);
  CHECK(Near(plain[0].lateral, plain[1].lateral, 0.01f));
  CHECK(plain[0].point.y > 0.0f); // left of +x is +y
  CHECK(plain[1].point.y < 0.0f);
  CHECK(plain[0].point.x < 200.0f); // a little back from the hit
  for (size_t index = 1; index < plain.size(); ++index)
    CHECK(plain[index - 1].cost <= plain[index].cost);

  // A side already taken on this route: its points all come before the other side's nearer ones.
  const auto sticky = MakeCandidates({200.0f, 0.0f, 0.0f}, 1.0f, 0.0f, -1, 1);
  CHECK_EQ(sticky[0].side, -1);
  CHECK_EQ(sticky[1].side, -1);
  CHECK_EQ(sticky[2].side, -1);
  // Only a hint: the hinted side first at the same distance, but a nearer point on the other side still wins.
  const auto hinted = MakeCandidates({200.0f, 0.0f, 0.0f}, 1.0f, 0.0f, 0, -1);
  CHECK_EQ(hinted[0].side, -1);
  CHECK_EQ(hinted[1].side, -1); // 130 on the hinted side costs less than 80 + 60 on the other
  CHECK_EQ(hinted[2].side, 1);
  CHECK(MakeCandidates({200.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 0, 0).empty());
}

TEST("path probe: the planner finds a waypoint with a free line to it and a free line back onto the path") {
  BoxWorld world;
  AddCar(world, 200.0f, -150.0f, 150.0f);
  const std::vector<Point3> path = {kOrigin, {1500.0f, 0.0f, 0.0f}};
  DetourPlanner planner = Plan(world, path);
  REQUIRE(planner.GetState() == DetourPlanner::State::Found);
  const Candidate &found = planner.Result();
  CHECK(found.status == CandidateStatus::Valid);
  CHECK(std::abs(found.point.y) > 150.0f + kSettings.halfWidth);
  CHECK(found.rejoin.x > 340.0f); // beyond the car
  CHECK(Near(found.rejoin.y, 0.0f, 0.5f));
  // Checked independently with the player's width.
  CHECK(Sweep(world, kOrigin, found.point, 1.0e9f, kSettings, kSettings.halfWidth).Clear());
  CHECK(Sweep(world, found.point, found.rejoin, 1.0e9f, kSettings, kSettings.halfWidth).Clear());
  // Nearer points were tried and thrown out first.
  CHECK(planner.Candidates().front().status == CandidateStatus::OnwardBlocked);
  CHECK(planner.Rays() > 0);
}

TEST("path probe: the planner goes round the short side of an obstacle") {
  BoxWorld world;
  AddCar(world, 200.0f, -250.0f, 50.0f); // it sticks out 50 to the left and 250 to the right
  DetourPlanner planner = Plan(world, {kOrigin, {1500.0f, 0.0f, 0.0f}});
  REQUIRE(planner.GetState() == DetourPlanner::State::Found);
  CHECK_EQ(planner.Result().side, 1);
  CHECK(planner.Result().lateral <= 130.5f);
}

TEST("path probe: a side already taken is kept while it still works") {
  BoxWorld world;
  AddCar(world, 200.0f, -150.0f, 150.0f);
  CHECK_EQ(Plan(world, {kOrigin, {1500.0f, 0.0f, 0.0f}}, -1).Result().side, -1);
  CHECK_EQ(Plan(world, {kOrigin, {1500.0f, 0.0f, 0.0f}}, 1).Result().side, 1);
}

TEST("path probe: points off the navmesh are not used") {
  BoxWorld world;
  AddCar(world, 200.0f, -150.0f, 150.0f);
  world.walkable = [](const Point3 &point) { return point.y < 0.0f; }; // nothing walkable on the left
  DetourPlanner planner = Plan(world, {kOrigin, {1500.0f, 0.0f, 0.0f}});
  REQUIRE(planner.GetState() == DetourPlanner::State::Found);
  CHECK_EQ(planner.Result().side, -1);
  bool sawRejected = false;
  for (const Candidate &candidate : planner.Candidates())
    sawRejected = sawRejected || candidate.status == CandidateStatus::NotWalkable;
  CHECK(sawRejected);
}

TEST("path probe: a wall with no way round fails the search") {
  BoxWorld world;
  world.Add({200.0f, -3000.0f, 0.0f}, {260.0f, 3000.0f, 200.0f});
  DetourPlanner planner = Plan(world, {kOrigin, {1500.0f, 0.0f, 0.0f}});
  CHECK(planner.GetState() == DetourPlanner::State::Failed);
  for (const Candidate &candidate : planner.Candidates())
    CHECK(candidate.status == CandidateStatus::OnwardBlocked);
}

TEST("path probe: a detour never leads off a ledge") {
  BoxWorld world;
  world.boxes.clear();
  world.Add({-1000.0f, -60.0f, -1000.0f}, {3000.0f, 60.0f, 0.0f}); // a walkway 120 wide, nothing beside it
  AddCar(world, 200.0f, -40.0f, 40.0f);
  DetourPlanner planner = Plan(world, {kOrigin, {1500.0f, 0.0f, 0.0f}});
  CHECK(planner.GetState() == DetourPlanner::State::Failed);
  for (const Candidate &candidate : planner.Candidates())
    CHECK(candidate.status == CandidateStatus::FirstLegBlocked);
}

TEST("path probe: a ray budget cuts the search into calls with the same outcome") {
  BoxWorld world;
  AddCar(world, 200.0f, -150.0f, 150.0f);
  const std::vector<Point3> path = {kOrigin, {1500.0f, 0.0f, 0.0f}};
  const DetourPlanner whole = Plan(world, path);

  const SweepResult blocked = SweepPath(world, path, 400.0f, kSettings, 0.0f);
  DetourPlanner planner;
  planner.Start(path, blocked, 0);
  int calls = 0;
  while (planner.GetState() == DetourPlanner::State::Working && calls < 1000) {
    const int before = planner.Rays();
    planner.Step(world, kOrigin, kSettings, 100);
    ++calls;
    // Never more than the budget plus the one sweep that crossed it (a leg of about 600 units with six rays a span).
    CHECK(planner.Rays() - before <= 100 + 250);
  }
  CHECK(calls > 1);
  REQUIRE(planner.GetState() == DetourPlanner::State::Found);
  CHECK_EQ(planner.Result().side, whole.Result().side);
  CHECK(Near(planner.Result().lateral, whole.Result().lateral, 0.01f));
  CHECK_EQ(planner.Rays(), whole.Rays());
}

TEST("path probe: an obstacle near the end of the path rejoins at the end") {
  BoxWorld world;
  world.Add({200.0f, -60.0f, 0.0f}, {260.0f, 60.0f, 120.0f});
  DetourPlanner planner = Plan(world, {kOrigin, {420.0f, 0.0f, 0.0f}});
  REQUIRE(planner.GetState() == DetourPlanner::State::Found);
  CHECK(Near(planner.Result().rejoin.x, 420.0f, 0.5f));
}

TEST("detour tracker: a detour ends on arrival or after too long") {
  DetourTracker tracker;
  CHECK(tracker.MayPlan());
  tracker.Begin({100.0f, 100.0f, 0.0f}, 1, 2000.0f);
  CHECK(tracker.Active());
  CHECK(!tracker.MayPlan());
  CHECK_EQ(tracker.PreferredSide(), 1);
  DetourTracker::End why = DetourTracker::End::Abandoned;
  CHECK(!tracker.Expired(0.0f, 0.0f, why));
  CHECK(tracker.Expired(80.0f, 80.0f, why));
  CHECK(why == DetourTracker::End::Arrived);
  for (int frame = 0; frame < 8 * 60 + 2; ++frame)
    tracker.Update(1.0f / 60.0f, 2000.0f);
  CHECK(tracker.Expired(0.0f, 0.0f, why));
  CHECK(why == DetourTracker::End::TimedOut);
  tracker.Finish();
  CHECK(!tracker.Active());
  CHECK(!tracker.Expired(0.0f, 0.0f, why));
  CHECK_EQ(tracker.PreferredSide(), 1); // the side outlives the detour
}

TEST("detour tracker: the budget runs out and only real progress brings it back") {
  DetourTracker tracker;
  for (int detour = 0; detour < DetourTracker::kBudget; ++detour) {
    REQUIRE(tracker.MayPlan());
    tracker.Begin({0.0f, 0.0f, 0.0f}, -1, 2000.0f - 10.0f * static_cast<float>(detour));
    tracker.Finish();
  }
  CHECK(!tracker.MayPlan());
  CHECK_EQ(tracker.Used(), DetourTracker::kBudget);
  // Time and a little progress do not refill it.
  for (int frame = 0; frame < 600; ++frame)
    tracker.Update(0.1f, 1900.0f);
  CHECK(!tracker.MayPlan());
  // 5 meters closer than the closest place a detour started from (1950) does, and the side is free again.
  tracker.Update(0.1f, 1950.0f - DetourTracker::kRefillProgress);
  CHECK(tracker.MayPlan());
  CHECK_EQ(tracker.Used(), 0);
  CHECK_EQ(tracker.PreferredSide(), 0);
}

TEST("detour tracker: a failed search waits before the next one") {
  DetourTracker tracker;
  tracker.PlanFailed();
  CHECK(!tracker.MayPlan());
  tracker.Update(1.0f, 2000.0f);
  CHECK(!tracker.MayPlan());
  tracker.Update(1.0f, 2000.0f);
  CHECK(tracker.MayPlan());
}

TEST("detour tracker: a new route forgets everything") {
  DetourTracker tracker;
  tracker.Begin({0.0f, 0.0f, 0.0f}, 1, 500.0f);
  tracker.PlanFailed();
  tracker.Reset();
  CHECK(!tracker.Active());
  CHECK(tracker.MayPlan());
  CHECK_EQ(tracker.PreferredSide(), 0);
  CHECK_EQ(tracker.Used(), 0);
}

TEST("block memory: a side-step is tried once per spot, then the pathfinder has to route around it") {
  BlockMemory memory;
  const Point3 car = {200.0f, 0.0f, 0.0f};
  CHECK(memory.Decide(car, false) == BlockMemory::Action::Search);
  memory.NoteDetour(car);
  // Blocked again a step further along the same car: the side-step did not get her past.
  const Point3 again = {260.0f, 60.0f, 0.0f};
  CHECK(memory.DetourTried(again));
  CHECK(memory.Decide(again, false) == BlockMemory::Action::Reroute);
  CHECK(memory.Avoid(again, 1.0f, 0.0f));
  CHECK_EQ(memory.AvoidedSpots().size(), 1u);
  // Blocked there again: straight to the pathfinder, and no second entry.
  CHECK(memory.Decide(car, false) == BlockMemory::Action::Reroute);
  CHECK(!memory.Avoid(car, 1.0f, 0.0f));
  // Another obstacle further on starts over with a side-step.
  CHECK(memory.Decide({900.0f, 0.0f, 0.0f}, false) == BlockMemory::Action::Search);
}

TEST("block memory: reroute-only mode skips the side-step, the oldest spot makes room, a new walk keeps the spots") {
  BlockMemory memory;
  CHECK(memory.Decide({0.0f, 0.0f, 0.0f}, true) == BlockMemory::Action::Reroute);
  for (size_t index = 0; index < BlockMemory::kMaxAvoided; ++index)
    CHECK(memory.Avoid({1000.0f * static_cast<float>(index), 0.0f, 0.0f}, 0.0f, 1.0f));
  CHECK(memory.Avoid({50000.0f, 0.0f, 0.0f}, 0.0f, 1.0f));
  CHECK_EQ(memory.AvoidedSpots().size(), BlockMemory::kMaxAvoided);
  CHECK(!memory.Avoided({0.0f, 0.0f, 0.0f})); // the first one was dropped
  CHECK(memory.Avoided({50000.0f, 0.0f, 0.0f}));

  memory.NoteDetour({7000.0f, 7000.0f, 0.0f});
  memory.ResetWalk();
  CHECK(!memory.DetourTried({7000.0f, 7000.0f, 0.0f}));
  CHECK(memory.Avoided({50000.0f, 0.0f, 0.0f}));
  memory.Reset();
  CHECK(memory.AvoidedSpots().empty());
  CHECK(memory.Decide({50000.0f, 0.0f, 0.0f}, false) == BlockMemory::Action::Search);
}

TEST("path probe: a floor too steep to stand on blocks, whatever its height") {
  // A world whose floor reports a steep normal beyond x = 200.
  class SteepWorld : public BoxWorld {
  public:
    RayHit Cast(const Point3 &from, const Point3 &to) override {
      RayHit hit = BoxWorld::Cast(from, to);
      if (hit.hit && from.x == to.x && from.y == to.y && from.x > 200.0f)
        hit.normalZ = 0.4f;
      return hit;
    }
  };
  SteepWorld world;
  const SweepResult sweep = Sweep(world, kOrigin, {600.0f, 0.0f, 0.0f}, 400.0f, kSettings, 0.0f);
  CHECK(sweep.blocker == Blocker::Rise);
  CHECK(Near(sweep.clearDistance, 200.0f, 0.5f));
}

TEST("detour tracker: a route of several waypoints is walked in order and can be skipped ahead") {
  DetourTracker tracker;
  tracker.Begin(std::vector<Point3>{{100.0f, 0.0f, 0.0f}, {100.0f, 100.0f, 0.0f}, {300.0f, 100.0f, 0.0f}}, 0, 900.0f);
  CHECK(tracker.Active());
  CHECK_EQ(tracker.Remaining().size(), 3u);
  DetourTracker::End why = DetourTracker::End::Abandoned;
  CHECK(!tracker.Expired(0.0f, 0.0f, why));
  CHECK(Near(tracker.Waypoint().x, 100.0f, 0.01f));
  // At the first: on to the second, not the end.
  CHECK(!tracker.Expired(90.0f, 10.0f, why));
  CHECK(Near(tracker.Waypoint().y, 100.0f, 0.01f));
  CHECK_EQ(tracker.Remaining().size(), 2u);
  // Each waypoint has its own time.
  for (int frame = 0; frame < 7 * 60; ++frame)
    tracker.Update(1.0f / 60.0f, 900.0f);
  CHECK(!tracker.Expired(90.0f, 95.0f, why));
  CHECK(Near(tracker.Waypoint().x, 300.0f, 0.01f));
  for (int frame = 0; frame < 7 * 60; ++frame)
    tracker.Update(1.0f / 60.0f, 900.0f);
  CHECK(!tracker.Expired(150.0f, 100.0f, why));
  CHECK(tracker.Expired(290.0f, 100.0f, why));
  CHECK(why == DetourTracker::End::Arrived);

  DetourTracker skipping;
  skipping.Begin(std::vector<Point3>{{1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}}, 0, 900.0f);
  skipping.SkipAhead(1);
  CHECK(Near(skipping.Waypoint().x, 2.0f, 0.01f));
  skipping.SkipAhead(5);
  CHECK(Near(skipping.Waypoint().x, 3.0f, 0.01f));
}

TEST("block memory: the same object seen from another side is the same obstacle") {
  BlockMemory memory;
  CHECK(memory.Avoid({200.0f, 0.0f, 0.0f}, 1.0f, 0.0f, 0xCA5));
  // 400 units away, on the far side of the same car.
  CHECK(memory.Avoided({600.0f, 0.0f, 0.0f}, 0xCA5));
  CHECK(memory.Decide({600.0f, 0.0f, 0.0f}, false, 0xCA5) == BlockMemory::Action::Reroute);
  CHECK(!memory.Avoid({600.0f, 0.0f, 0.0f}, -1.0f, 0.0f, 0xCA5));
  // Another object there, or terrain, is a new obstacle.
  CHECK(memory.Decide({600.0f, 0.0f, 0.0f}, false, 0xBEE) == BlockMemory::Action::Search);
  CHECK(memory.Decide({600.0f, 0.0f, 0.0f}, false, 0) == BlockMemory::Action::Search);
}
