#pragma once

// Engine-free part of the Walker debug mode (todo 43): what the Walker steered by on its last route search, the
// player's trail, whether two route searches gave the same route (so the log only speaks when it changes), the log
// text, and the overlay drawing in screen space. WalkerHandler fills `View` from engine reads while debug is on;
// src/Debug/WalkerOverlay.cpp draws it. Nothing here changes how the Walker walks.

#include "Debug/OverlayMath.hpp"
#include "Movement/PathProbe.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace RouteDebug {

using Overlay::Point3;

// How the steering point of a route search was found (WalkerHandler's ResolveNavigationPoint).
enum class Mode {
  None,     // nothing searched yet
  Close,    // the destination is within the interaction distance: steer at it directly, no search
  Path,     // the pathfinder found a route to the destination
  Snapped,  // no route to the destination itself; one to the closest navmesh point next to it
  Straight, // both searches failed: steer in a straight line at the destination
};

const char *ModeName(Mode mode);

struct Route {
  Mode mode = Mode::None;
  std::vector<Point3> nodes; // the pathfinder's nodes, in order (empty for Close and Straight)
  int chosen = -1;           // index of the node steered at, -1 when there are no nodes
  Point3 steer;              // the point the Walker steered at
  Point3 destination;        // what the search was for: the target, or the Wander spot
  bool detour = false;       // the destination is a recovery Wander spot, not the target
  bool hasSnapped = false;
  Point3 snapped; // Snapped (and a failed snap attempt in Straight): the closest navmesh point to the destination
  Point3 start;   // where the player stood when the search ran
  // Suspects from the todo: whether the player stood on the navmesh and which cells the search used.
  bool playerOnNavmesh = false;
  uint32_t searchCell = 0; // GetNavigationCell (the interior, or the player's cell)
  uint32_t markerCell = 0; // the waypoint marker's parentCell, used as the destination's cell
  uint32_t playerCell = 0;
  uint32_t targetCell = 0;
};

// Whether two searches gave the same route: same mode and node count, same chosen node, and every node (except the
// first, which is where the player stood) within `tolerance` units of its counterpart. Close and Straight compare the
// steering point instead.
bool SameRoute(const Route &a, const Route &b, float tolerance = 30.0f);

// Total length of the nodes from the first to the last, in game units.
float PathLength(const Route &route);

// One log line: mode, node count and length, the node steered at and how far it is from `player`, the cells, and
// every node's position.
std::string Describe(const Route &route, const Point3 &player);

// Where the player went: a point every `kSpacing` units, at most `kMaxPoints` (the oldest are dropped).
class Trail {
public:
  static constexpr float kSpacing = 25.0f;
  static constexpr size_t kMaxPoints = 800;

  void Clear() { m_points.clear(); }
  void Add(const Point3 &p);
  const std::vector<Point3> &Points() const { return m_points; }

private:
  std::vector<Point3> m_points;
};

// The raycast path check (todo 48): the rays of the last sweep of the path, where it was blocked, the points the last
// search for a way round tried, and the detour being walked.
struct ProbeView {
  std::vector<PathProbe::TraceRay> rays;
  std::vector<PathProbe::Candidate> candidates;
  bool hasBlock = false;
  Point3 block;
  bool hasWaypoint = false;
  Point3 waypoint;
  Point3 rejoin; // where the detour meets the path again
};

struct View {
  bool hasRoute = false;
  Route route;
  bool active = false; // a command is still running; the last route stays visible (dimmed) after it ends
  Point3 player;
  bool hasTarget = false;
  Point3 target;
  std::vector<Point3> trail;
  ProbeView probe;
};

// Colours (ARGB). The route's line colour says how it was found.
inline constexpr uint32_t kColourPath = 0xFF00E0FF;     // cyan: a navmesh route to the destination
inline constexpr uint32_t kColourSnapped = 0xFFFF9900;  // orange: route to the navmesh point next to the destination
inline constexpr uint32_t kColourStraight = 0xFFFF2020; // red: straight line, no route found
inline constexpr uint32_t kColourClose = 0xFFFFFFFF;    // white: close enough to steer directly
inline constexpr uint32_t kColourNode = 0xFFFFE000;     // yellow: a path node
inline constexpr uint32_t kColourChosen = 0xFF20FF20;   // green: the node steered at, and the line to it
inline constexpr uint32_t kColourTarget = 0xFFFFFFFF;   // white: the command's target
inline constexpr uint32_t kColourDetour = 0xFFFF40FF;   // magenta: a Wander spot during recovery
inline constexpr uint32_t kColourTrail = 0xB04060FF;    // blue: where the player actually went
inline constexpr uint32_t kColourLabel = 0xFFFFFFFF;
inline constexpr uint32_t kColourRayClear = 0x90C8C8C8;   // grey: a probe ray that hit nothing
inline constexpr uint32_t kColourRayBlocked = 0xFFFF3060; // red: a probe ray up to where it was blocked
inline constexpr uint32_t kColourWaypoint = 0xFFB070FF;   // violet: the detour waypoint and its two legs
inline constexpr uint32_t kColourRejected = 0xC0808080;   // dark grey: a point the search for a way round threw out

uint32_t ModeColour(Mode mode);

// Builds the overlay for `view` seen through `camera`. `scale` multiplies every pixel size (1 at 1080 lines).
void BuildOverlay(const View &view, const Overlay::Camera &camera, float scale, Overlay::Batch &batch);

} // namespace RouteDebug
