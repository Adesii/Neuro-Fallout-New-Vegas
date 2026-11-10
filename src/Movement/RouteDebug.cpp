#include "Movement/RouteDebug.hpp"

#include "Context/Distance.hpp"
#include <cmath>
#include <cstdio>

namespace RouteDebug {
namespace {

float Distance(const Point3 &a, const Point3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Point3 Raised(const Point3 &p, float height) { return {p.x, p.y, p.z + height}; }

void AppendPoint(std::string &out, const Point3 &p) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "(%.0f, %.0f, %.0f)", p.x, p.y, p.z);
  out += buffer;
}

// Heights of the vertical poles drawn at points, in game units: they make a point on the ground easy to place in
// depth when it is far away or behind something.
constexpr float kNodePole = 40.0f;
constexpr float kMarkerPole = 100.0f;

} // namespace

const char *ModeName(Mode mode) {
  switch (mode) {
  case Mode::None:
    return "none";
  case Mode::Close:
    return "close (steering at the destination directly)";
  case Mode::Path:
    return "navmesh path";
  case Mode::Snapped:
    return "path to the navmesh point next to the destination";
  case Mode::Straight:
    return "STRAIGHT LINE (no path found)";
  }
  return "?";
}

uint32_t ModeColour(Mode mode) {
  switch (mode) {
  case Mode::Path:
    return kColourPath;
  case Mode::Snapped:
    return kColourSnapped;
  case Mode::Straight:
    return kColourStraight;
  case Mode::Close:
  case Mode::None:
    break;
  }
  return kColourClose;
}

bool SameRoute(const Route &a, const Route &b, float tolerance) {
  if (a.mode != b.mode || a.nodes.size() != b.nodes.size() || a.chosen != b.chosen || a.detour != b.detour)
    return false;
  if (a.nodes.empty())
    return Distance(a.steer, b.steer) <= tolerance;
  for (size_t index = 1; index < a.nodes.size(); ++index)
    if (Distance(a.nodes[index], b.nodes[index]) > tolerance)
      return false;
  return true;
}

float PathLength(const Route &route) {
  float length = 0.0f;
  for (size_t index = 1; index < route.nodes.size(); ++index)
    length += Distance(route.nodes[index - 1], route.nodes[index]);
  return length;
}

std::string Describe(const Route &route, const Point3 &player) {
  char buffer[256];
  std::string out = ModeName(route.mode);
  if (!route.nodes.empty()) {
    std::snprintf(buffer, sizeof(buffer), ", %zu nodes, %.1f m", route.nodes.size(),
                  Units::ToMeters(PathLength(route)));
    out += buffer;
  }
  if (route.chosen >= 0)
    std::snprintf(buffer, sizeof(buffer), "; steering at node %d, %.1f m away", route.chosen,
                  Units::ToMeters(Distance(route.steer, player)));
  else
    std::snprintf(buffer, sizeof(buffer), "; steering %.1f m away", Units::ToMeters(Distance(route.steer, player)));
  out += buffer;
  out += route.detour ? "; destination: Wander spot " : "; destination: target ";
  AppendPoint(out, route.destination);
  if (route.hasSnapped) {
    out += ", closest navmesh point ";
    AppendPoint(out, route.snapped);
  }
  out += "; player ";
  AppendPoint(out, route.start);
  out += route.playerOnNavmesh ? " on navmesh" : " NOT on navmesh";
  std::snprintf(buffer, sizeof(buffer), "; cells: search %08X, marker %08X, player %08X, target %08X",
                static_cast<unsigned>(route.searchCell), static_cast<unsigned>(route.markerCell),
                static_cast<unsigned>(route.playerCell), static_cast<unsigned>(route.targetCell));
  out += buffer;
  if (!route.nodes.empty()) {
    out += "; nodes:";
    for (size_t index = 0; index < route.nodes.size(); ++index) {
      std::snprintf(buffer, sizeof(buffer), " %zu", index);
      out += buffer;
      AppendPoint(out, route.nodes[index]);
    }
  }
  return out;
}

void Trail::Add(const Point3 &p) {
  if (!m_points.empty() && Distance(m_points.back(), p) < kSpacing)
    return;
  if (m_points.size() >= kMaxPoints)
    m_points.erase(m_points.begin(), m_points.begin() + static_cast<std::ptrdiff_t>(kMaxPoints / 4));
  m_points.push_back(p);
}

void BuildOverlay(const View &view, const Overlay::Camera &camera, float scale, Overlay::Batch &batch) {
  if (!Overlay::UsableCamera(camera))
    return;
  const float alpha = view.active ? 1.0f : 0.45f;
  auto colour = [alpha](uint32_t c) { return Overlay::Fade(c, alpha); };
  const float thin = 1.5f * scale;
  const float line = 3.0f * scale;

  for (size_t index = 1; index < view.trail.size(); ++index)
    batch.WorldLine(camera, view.trail[index - 1], view.trail[index], 2.0f * scale, colour(kColourTrail));

  if (view.hasRoute) {
    const Route &route = view.route;
    const uint32_t routeColour = colour(ModeColour(route.mode));
    if (route.nodes.empty()) {
      // Close or Straight: the Walker heads straight from where it stood at the destination.
      batch.WorldLine(camera, route.start, route.destination, route.mode == Mode::Straight ? line : thin, routeColour);
    } else {
      for (size_t index = 1; index < route.nodes.size(); ++index)
        batch.WorldLine(camera, route.nodes[index - 1], route.nodes[index], line, routeColour);
      // The last node to the destination itself, thin: a gap here is the part the path does not cover.
      batch.WorldLine(camera, route.nodes.back(), route.destination, thin, routeColour);
      for (size_t index = 0; index < route.nodes.size(); ++index) {
        const Point3 &node = route.nodes[index];
        batch.WorldLine(camera, node, Raised(node, kNodePole), thin, colour(kColourNode));
        Overlay::ScreenPoint at;
        if (batch.WorldBox(camera, node, 4.0f * scale, colour(kColourNode), &at))
          batch.Number({at.x + 6.0f * scale, at.y - 20.0f * scale}, 12.0f * scale, static_cast<unsigned>(index),
                       colour(kColourLabel));
      }
    }
    if (route.hasSnapped) {
      Overlay::ScreenPoint at;
      if (Overlay::Project(camera, route.snapped, at))
        batch.Frame(at, 8.0f * scale, thin, colour(kColourSnapped));
    }
    if (route.detour) {
      batch.WorldLine(camera, route.destination, Raised(route.destination, kMarkerPole), line, colour(kColourDetour));
      batch.WorldBox(camera, route.destination, 7.0f * scale, colour(kColourDetour));
    }
    // What the Walker steers at right now, and the line from the player to it.
    batch.WorldLine(camera, view.player, route.steer, 2.0f * scale, colour(kColourChosen));
    batch.WorldLine(camera, route.steer, Raised(route.steer, kNodePole), line, colour(kColourChosen));
    Overlay::ScreenPoint at;
    if (Overlay::Project(camera, route.steer, at))
      batch.Frame(at, 9.0f * scale, 2.0f * scale, colour(kColourChosen));
  }

  // The raycast path check: the rays of the last sweep, the points tried beside the obstacle, and the detour.
  const ProbeView &probe = view.probe;
  for (const PathProbe::TraceRay &ray : probe.rays) {
    batch.WorldLine(camera, ray.from, ray.to, ray.blocked ? thin * 1.5f : thin,
                    colour(ray.blocked ? kColourRayBlocked : kColourRayClear));
    if (ray.blocked)
      batch.WorldBox(camera, ray.to, 3.0f * scale, colour(kColourRayBlocked));
  }
  if (probe.hasBlock) {
    batch.WorldLine(camera, probe.block, Raised(probe.block, kMarkerPole), line, colour(kColourRayBlocked));
    Overlay::ScreenPoint at;
    if (Overlay::Project(camera, probe.block, at))
      batch.Frame(at, 8.0f * scale, 2.0f * scale, colour(kColourRayBlocked));
  }
  for (const PathProbe::Candidate &candidate : probe.candidates) {
    if (candidate.status == PathProbe::CandidateStatus::Pending)
      continue;
    const bool valid = candidate.status == PathProbe::CandidateStatus::Valid;
    Overlay::ScreenPoint at;
    if (Overlay::Project(camera, candidate.point, at))
      batch.Frame(at, 5.0f * scale, thin, colour(valid ? kColourWaypoint : kColourRejected));
  }
  if (probe.hasWaypoint) {
    batch.WorldLine(camera, view.player, probe.waypoint, line, colour(kColourWaypoint));
    batch.WorldLine(camera, probe.waypoint, probe.rejoin, thin, colour(kColourWaypoint));
    batch.WorldLine(camera, probe.waypoint, Raised(probe.waypoint, kMarkerPole), line, colour(kColourWaypoint));
    batch.WorldBox(camera, probe.waypoint, 7.0f * scale, colour(kColourWaypoint));
  }

  if (view.hasTarget) {
    batch.WorldLine(camera, view.target, Raised(view.target, kMarkerPole), line, colour(kColourTarget));
    Overlay::ScreenPoint at;
    if (Overlay::Project(camera, view.target, at))
      batch.Frame(at, 10.0f * scale, 2.0f * scale, colour(kColourTarget));
  }
}

} // namespace RouteDebug
