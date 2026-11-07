#include "Movement/RouteDebug.hpp"
#include "TestFramework.hpp"
#include <string>

using namespace RouteDebug;

namespace {

Route PathRoute() {
  Route route;
  route.mode = Mode::Path;
  route.nodes = {{0.0f, 0.0f, 0.0f}, {100.0f, 0.0f, 0.0f}, {100.0f, 200.0f, 0.0f}};
  route.chosen = 1;
  route.steer = route.nodes[1];
  route.destination = {100.0f, 300.0f, 0.0f};
  route.start = {0.0f, 0.0f, 0.0f};
  return route;
}

Overlay::Camera TestCamera() {
  Overlay::Camera camera;
  // Looking along +y from (0, -500, 100): clip x = x, clip y = z - 100, w = y + 500.
  camera.worldToClip[0][0] = 1.0f;
  camera.worldToClip[1][2] = 1.0f;
  camera.worldToClip[1][3] = -100.0f;
  camera.worldToClip[2][1] = 1.0f;
  camera.worldToClip[3][1] = 1.0f;
  camera.worldToClip[3][3] = 500.0f;
  camera.width = 1920.0f;
  camera.height = 1080.0f;
  return camera;
}

} // namespace

TEST("route debug: the same route searched again from a step further is the same route") {
  const Route a = PathRoute();
  Route b = a;
  b.nodes[0] = {20.0f, 5.0f, 0.0f}; // the first node is where the player stood
  b.start = b.nodes[0];
  CHECK(SameRoute(a, b));
  b.nodes[2].x += 10.0f; // jitter under the tolerance
  CHECK(SameRoute(a, b));
}

TEST("route debug: a different route, mode, node count or chosen node is a change") {
  const Route a = PathRoute();
  Route moved = a;
  moved.nodes[2].x += 100.0f;
  CHECK(!SameRoute(a, moved));
  Route mode = a;
  mode.mode = Mode::Snapped;
  CHECK(!SameRoute(a, mode));
  Route shorter = a;
  shorter.nodes.pop_back();
  CHECK(!SameRoute(a, shorter));
  Route chosen = a;
  chosen.chosen = 2;
  CHECK(!SameRoute(a, chosen));
  Route detour = a;
  detour.detour = true;
  CHECK(!SameRoute(a, detour));
}

TEST("route debug: routes without nodes compare their steering point") {
  Route a;
  a.mode = Mode::Straight;
  a.steer = {500.0f, 0.0f, 0.0f};
  Route b = a;
  b.steer.x += 10.0f;
  CHECK(SameRoute(a, b));
  b.steer.x += 100.0f;
  CHECK(!SameRoute(a, b));
}

TEST("route debug: path length sums the segments") { CHECK_EQ(PathLength(PathRoute()), 300.0f); }

TEST("route debug: the log line names the mode, the chosen node, the navmesh check and every node") {
  Route route = PathRoute();
  route.searchCell = 0x0001ABCD;
  const std::string text = Describe(route, {0.0f, 0.0f, 0.0f});
  CHECK(text.find("navmesh path") != std::string::npos);
  CHECK(text.find("3 nodes") != std::string::npos);
  CHECK(text.find("steering at node 1") != std::string::npos);
  CHECK(text.find("NOT on navmesh") != std::string::npos);
  CHECK(text.find("search 0001ABCD") != std::string::npos);
  CHECK(text.find("2(100, 200, 0)") != std::string::npos);
  Route straight;
  straight.mode = Mode::Straight;
  CHECK(Describe(straight, {}).find("STRAIGHT LINE") != std::string::npos);
}

TEST("route debug: the trail keeps a point every few units and drops the oldest past the cap") {
  Trail trail;
  trail.Add({0.0f, 0.0f, 0.0f});
  trail.Add({5.0f, 0.0f, 0.0f});
  CHECK_EQ(trail.Points().size(), size_t{1});
  trail.Add({Trail::kSpacing + 1.0f, 0.0f, 0.0f});
  CHECK_EQ(trail.Points().size(), size_t{2});
  for (size_t i = 0; i < Trail::kMaxPoints * 2; ++i)
    trail.Add({static_cast<float>(i) * 100.0f, 1000.0f, 0.0f});
  CHECK(trail.Points().size() <= Trail::kMaxPoints);
  CHECK_EQ(trail.Points().back().x, static_cast<float>(Trail::kMaxPoints * 2 - 1) * 100.0f);
}

TEST("route debug: the overlay draws the route, its nodes, the steering point and the target") {
  View view;
  view.hasRoute = true;
  view.route = PathRoute();
  view.active = true;
  view.hasTarget = true;
  view.target = view.route.destination;
  Overlay::Batch batch;
  BuildOverlay(view, TestCamera(), 1.0f, batch);
  CHECK(batch.Triangles() > 0);
  bool sawPath = false;
  bool sawChosen = false;
  for (const auto &v : batch.Vertices()) {
    sawPath |= v.color == kColourPath;
    sawChosen |= v.color == kColourChosen;
  }
  CHECK(sawPath);
  CHECK(sawChosen);
}

TEST("route debug: a finished command's route stays drawn, dimmed") {
  View view;
  view.hasRoute = true;
  view.route = PathRoute();
  view.active = false;
  Overlay::Batch batch;
  BuildOverlay(view, TestCamera(), 1.0f, batch);
  CHECK(batch.Triangles() > 0);
  for (const auto &v : batch.Vertices())
    CHECK((v.color >> 24) < 0xFF);
}

TEST("route debug: nothing is drawn through an unusable camera") {
  View view;
  view.hasRoute = true;
  view.route = PathRoute();
  Overlay::Batch batch;
  BuildOverlay(view, Overlay::Camera{}, 1.0f, batch);
  CHECK_EQ(batch.Triangles(), size_t{0});
}
