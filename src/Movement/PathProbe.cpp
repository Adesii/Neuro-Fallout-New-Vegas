#include "Movement/PathProbe.hpp"

#include <algorithm>
#include <cmath>

namespace PathProbe {
namespace {

constexpr float kEpsilon = 0.5f;
// Longer than any leg the planner makes: the sweep's own length decides.
constexpr float kNoLimit = 1.0e9f;
// Candidate points sit this far back from the blocked spot, so they are beside the near face of the obstacle and not
// level with its inside.
constexpr float kCandidateBackOff = 45.0f;
constexpr float kCandidateLaterals[] = {80.0f, 130.0f, 190.0f, 260.0f, 340.0f, 430.0f};

float Distance2D(const Point3 &a, const Point3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

Point3 Lerp(const Point3 &a, const Point3 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

} // namespace

const char *BlockerName(Blocker blocker) {
  switch (blocker) {
  case Blocker::None:
    return "nothing";
  case Blocker::Obstacle:
    return "an obstacle";
  case Blocker::Rise:
    return "a rise too high to step onto";
  case Blocker::Drop:
    return "a drop";
  case Blocker::Door:
    return "a door";
  }
  return "?";
}

void Trace::Add(const Point3 &from, const Point3 &to, bool blocked) {
  if (rays.size() < kMaxRays)
    rays.push_back({from, to, blocked});
}

SweepResult Sweep(World &world, const Point3 &from, const Point3 &to, float maxDistance, const Settings &settings,
                  float halfWidth, Trace *trace) {
  SweepResult result;
  result.end = from;
  result.blockPoint = from;
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (!(length > kEpsilon) || !(maxDistance > kEpsilon))
    return result;
  const float ux = dx / length;
  const float uy = dy / length;
  const float nx = -uy; // left of the direction of travel
  const float ny = ux;
  result.dirX = ux;
  result.dirY = uy;
  const float total = std::min(length, maxDistance);
  const float step = std::max(settings.groundStep, 1.0f);
  const int span = std::max(settings.spanSamples, 1);
  const float heights[2] = {settings.kneeHeight, settings.chestHeight};
  // The centre line alone, or the centre line and a ray to either side of it.
  const float offsets[3] = {0.0f, halfWidth, -halfWidth};
  const int offsetCount = halfWidth > 0.0f ? 3 : 1;

  Point3 floor = from;
  Point3 spanStart = from;
  float spanStartDistance = 0.0f;
  // The forward rays start one floor sample before their span, so two spans overlap: a ray that starts exactly on a
  // face, or just inside a convex shape, does not hit it, and the face would slip between two spans.
  Point3 rayStart = from;
  float rayStartDistance = 0.0f;
  Point3 previous = from;
  float previousDistance = 0.0f;
  float distance = 0.0f;
  int inSpan = 0;
  while (distance < total - kEpsilon) {
    // The floor under the next sample.
    const float next = std::min(distance + step, total);
    const Point3 above = {from.x + ux * next, from.y + uy * next, floor.z + settings.floorAbove};
    const Point3 below = {above.x, above.y, floor.z - settings.floorBelow};
    const RayHit down = world.Cast(above, below);
    ++result.rays;
    if (down.hit && down.kind == HitKind::Target) {
      result.reachedTarget = true;
      result.clearDistance = distance;
      result.end = floor;
      return result;
    }
    float z = floor.z; // someone standing there says nothing about the ground: assume it is level
    Blocker blocker = Blocker::None;
    if (!down.hit) {
      blocker = Blocker::Drop;
    } else if (down.kind != HitKind::Actor) {
      z = above.z + (below.z - above.z) * down.fraction;
      if (z - floor.z > settings.maxRise || down.normalZ < settings.minFloorNormalZ)
        blocker = Blocker::Rise;
      else if (floor.z - z > settings.maxDrop)
        blocker = Blocker::Drop;
    }
    if (blocker != Blocker::None) {
      result.blocker = blocker;
      result.hit = down;
      result.step = down.hit ? z - floor.z : 0.0f;
      result.blockPoint = {above.x, above.y, floor.z};
      result.clearDistance = distance;
      result.end = floor;
      if (trace)
        trace->Add(above, {above.x, above.y, down.hit ? z : below.z}, true);
      return result;
    }
    previous = floor;
    previousDistance = distance;
    floor = {above.x, above.y, z};
    distance = next;
    ++inSpan;
    if (inSpan < span && distance < total - kEpsilon)
      continue;

    // Forward rays over the span just sampled, at each height above the floor found at its two ends.
    float nearestBlock = 2.0f;
    float nearestTarget = 2.0f;
    RayHit blockHit;
    bool left = false;
    bool right = false;
    const bool lastSpan = distance >= total - kEpsilon;
    const float past = lastSpan ? std::max(settings.endClearance, 0.0f) : 0.0f;
    const Point3 rayEnd = {floor.x + ux * past, floor.y + uy * past, floor.z};
    const float rayEndDistance = distance + past;
    for (float height : heights) {
      for (int index = 0; index < offsetCount; ++index) {
        const float offset = offsets[index];
        const Point3 a = {rayStart.x + nx * offset, rayStart.y + ny * offset, rayStart.z + height};
        const Point3 b = {rayEnd.x + nx * offset, rayEnd.y + ny * offset, rayEnd.z + height};
        const RayHit hit = world.Cast(a, b);
        ++result.rays;
        const bool blocks = hit.hit && hit.kind != HitKind::Actor && hit.kind != HitKind::Target;
        if (trace)
          trace->Add(a, blocks ? Lerp(a, b, hit.fraction) : b, blocks);
        if (hit.hit && hit.kind == HitKind::Target)
          nearestTarget = std::min(nearestTarget, hit.fraction);
        if (!blocks)
          continue;
        if (offset >= 0.0f)
          left = true;
        if (offset <= 0.0f)
          right = true;
        if (hit.fraction < nearestBlock) {
          nearestBlock = hit.fraction;
          blockHit = hit;
        }
      }
    }
    const float nearest = std::min(nearestBlock, nearestTarget);
    if (nearest <= 1.0f) {
      // Whichever comes first along the rays: the target (the walk ends there) or something in the way.
      result.blockPoint = Lerp(rayStart, rayEnd, nearest);
      result.clearDistance = std::min(rayStartDistance + (rayEndDistance - rayStartDistance) * nearest, distance);
      result.end = result.clearDistance >= spanStartDistance ? spanStart : rayStart;
      if (nearestTarget <= nearestBlock) {
        result.reachedTarget = true;
        return result;
      }
      result.blocker = blockHit.kind == HitKind::Door ? Blocker::Door : Blocker::Obstacle;
      result.hit = blockHit;
      result.blockedSide = left && !right ? 1 : right && !left ? -1 : 0;
      return result;
    }
    result.clearDistance = distance;
    result.end = floor;
    spanStart = floor;
    spanStartDistance = distance;
    rayStart = previous;
    rayStartDistance = previousDistance;
    inSpan = 0;
  }
  return result;
}

SweepResult SweepPath(World &world, const std::vector<Point3> &points, float maxDistance, const Settings &settings,
                      float halfWidth, Trace *trace) {
  SweepResult total;
  if (points.empty())
    return total;
  total.end = points[0];
  total.blockPoint = points[0];
  float travelled = 0.0f;
  for (size_t index = 1; index < points.size(); ++index) {
    const float remaining = maxDistance - travelled;
    if (!(remaining > kEpsilon))
      break;
    const SweepResult leg = Sweep(world, total.end, points[index], remaining, settings, halfWidth, trace);
    total.rays += leg.rays;
    if (leg.dirX != 0.0f || leg.dirY != 0.0f) {
      total.dirX = leg.dirX;
      total.dirY = leg.dirY;
    }
    travelled += leg.clearDistance;
    total.end = leg.end;
    if (!leg.Clear() || leg.reachedTarget) {
      total.blocker = leg.blocker;
      total.reachedTarget = leg.reachedTarget;
      total.hit = leg.hit;
      total.step = leg.step;
      total.blockPoint = leg.blockPoint;
      total.blockedSide = leg.blockedSide;
      break;
    }
  }
  total.clearDistance = travelled;
  return total;
}

float PathLength(const std::vector<Point3> &points) {
  float length = 0.0f;
  for (size_t index = 1; index < points.size(); ++index)
    length += Distance2D(points[index - 1], points[index]);
  return length;
}

Point3 PointAlong(const std::vector<Point3> &points, float distance) {
  if (points.empty())
    return {};
  if (!(distance > 0.0f))
    return points.front();
  for (size_t index = 1; index < points.size(); ++index) {
    const float leg = Distance2D(points[index - 1], points[index]);
    if (distance <= leg)
      return leg > 0.0f ? Lerp(points[index - 1], points[index], distance / leg) : points[index];
    distance -= leg;
  }
  return points.back();
}

std::vector<Point3> TrimEnd(std::vector<Point3> points, float trim) {
  while (points.size() > 1 && trim > 0.0f) {
    const Point3 &last = points.back();
    const Point3 &before = points[points.size() - 2];
    const float leg = Distance2D(before, last);
    if (leg > trim) {
      points.back() = Lerp(last, before, trim / leg);
      break;
    }
    trim -= leg;
    points.pop_back();
  }
  return points;
}

std::vector<Candidate> MakeCandidates(const Point3 &blockPoint, float dirX, float dirY, int preferredSide,
                                      int hintSide) {
  std::vector<Candidate> candidates;
  if (dirX == 0.0f && dirY == 0.0f)
    return candidates;
  const float nx = -dirY;
  const float ny = dirX;
  const Point3 base = {blockPoint.x - dirX * kCandidateBackOff, blockPoint.y - dirY * kCandidateBackOff, blockPoint.z};
  for (float lateral : kCandidateLaterals) {
    for (int side : {1, -1}) {
      Candidate candidate;
      candidate.point = {base.x + nx * lateral * static_cast<float>(side),
                         base.y + ny * lateral * static_cast<float>(side), base.z};
      candidate.side = side;
      candidate.lateral = lateral;
      candidate.cost = lateral;
      if (preferredSide != 0 && side != preferredSide)
        candidate.cost += kSidePenalty;
      else if (preferredSide == 0 && hintSide != 0 && side != hintSide)
        candidate.cost += kHintPenalty;
      candidates.push_back(candidate);
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate &a, const Candidate &b) { return a.cost < b.cost; });
  return candidates;
}

void DetourPlanner::Reset() { *this = {}; }

void DetourPlanner::Start(std::vector<Point3> path, const SweepResult &blocked, int preferredSide) {
  Reset();
  m_path = std::move(path);
  m_blockDistance = blocked.clearDistance;
  // Only the left ray blocked means the obstacle is on the left: the way round is more likely on the right.
  m_candidates = MakeCandidates(blocked.blockPoint, blocked.dirX, blocked.dirY, preferredSide, -blocked.blockedSide);
  m_state = m_candidates.empty() ? State::Failed : State::Working;
}

DetourPlanner::State DetourPlanner::Step(World &world, const Point3 &player, const Settings &settings, int rayBudget,
                                         Trace *trace) {
  if (m_state != State::Working)
    return m_state;
  const float length = PathLength(m_path);
  int spent = 0;
  auto count = [&](const SweepResult &sweep) {
    spent += sweep.rays;
    m_rays += sweep.rays;
  };
  auto nextCandidate = [&](CandidateStatus status) {
    m_candidates[m_index].status = status;
    ++m_index;
    m_stage = 0;
  };
  while (m_index < m_candidates.size()) {
    if (spent >= rayBudget)
      return m_state;
    Candidate &candidate = m_candidates[m_index];
    if (m_stage == 0) {
      const SweepResult leg = Sweep(world, player, candidate.point, kNoLimit, settings, settings.halfWidth, trace);
      count(leg);
      if (!leg.Clear() || leg.reachedTarget) {
        nextCandidate(CandidateStatus::FirstLegBlocked);
        continue;
      }
      candidate.point = leg.end; // now on the floor
      if (!world.Walkable(candidate.point)) {
        nextCandidate(CandidateStatus::NotWalkable);
        continue;
      }
      m_stage = 1;
      continue;
    }
    const float ahead = m_stage == 1 ? kRejoinNear : kRejoinFar;
    const Point3 rejoin = PointAlong(m_path, std::min(m_blockDistance + ahead, length));
    const SweepResult onward = Sweep(world, candidate.point, rejoin, kNoLimit, settings, settings.halfWidth, trace);
    count(onward);
    if (onward.Clear()) {
      candidate.status = CandidateStatus::Valid;
      candidate.rejoin = {rejoin.x, rejoin.y, onward.reachedTarget ? rejoin.z : onward.end.z};
      m_state = State::Found;
      return m_state;
    }
    // The further rejoin point only differs when the path goes on that far.
    if (m_stage == 1 && m_blockDistance + kRejoinNear < length - kEpsilon) {
      m_stage = 2;
      continue;
    }
    nextCandidate(CandidateStatus::OnwardBlocked);
  }
  m_state = State::Failed;
  return m_state;
}

const char *DetourTracker::EndName(End end) {
  switch (end) {
  case End::Arrived:
    return "reached the waypoint";
  case End::PathClear:
    return "the path is clear again";
  case End::LegBlocked:
    return "the way to the waypoint got blocked";
  case End::TimedOut:
    return "took too long";
  case End::Abandoned:
    return "abandoned";
  }
  return "?";
}

void DetourTracker::Reset() { *this = {}; }

void DetourTracker::Update(float dt, float remaining) {
  if (m_cooldown > 0.0f)
    m_cooldown -= dt;
  if (m_active)
    m_elapsed += dt;
  if (m_hasStart && !m_active && remaining <= m_bestStart - kRefillProgress) {
    // Really past it: the next obstacle is a new one, with a full budget and no side to stick to.
    m_used = 0;
    m_side = 0;
    m_hasStart = false;
  }
}

void DetourTracker::Begin(const Point3 &waypoint, int side, float remaining) {
  Begin(std::vector<Point3>{waypoint}, side, remaining);
}

void DetourTracker::Begin(std::vector<Point3> waypoints, int side, float remaining) {
  if (waypoints.empty())
    return;
  m_active = true;
  m_waypoints = std::move(waypoints);
  m_index = 0;
  m_side = side;
  m_elapsed = 0.0f;
  ++m_used;
  if (!m_hasStart || remaining < m_bestStart) {
    m_bestStart = remaining;
    m_hasStart = true;
  }
}

void DetourTracker::Finish() {
  m_active = false;
  m_elapsed = 0.0f;
}

std::vector<Point3> DetourTracker::Remaining() const {
  if (!m_active)
    return {};
  return {m_waypoints.begin() + static_cast<std::ptrdiff_t>(m_index), m_waypoints.end()};
}

void DetourTracker::SkipAhead(size_t count) {
  if (!m_active || count == 0)
    return;
  m_index = std::min(m_index + count, m_waypoints.size() - 1);
  m_elapsed = 0.0f;
}

bool DetourTracker::Expired(float playerX, float playerY, End &why) {
  if (!m_active)
    return false;
  // Several waypoints can be within reach at once where the route turns tightly.
  while (true) {
    const float dx = playerX - m_waypoints[m_index].x;
    const float dy = playerY - m_waypoints[m_index].y;
    if (std::sqrt(dx * dx + dy * dy) > kArriveDistance)
      break;
    if (m_index + 1 >= m_waypoints.size()) {
      why = End::Arrived;
      return true;
    }
    ++m_index;
    m_elapsed = 0.0f; // every waypoint gets its own time
  }
  if (m_elapsed >= kMaxSeconds) {
    why = End::TimedOut;
    return true;
  }
  return false;
}

void BlockMemory::Reset() { *this = {}; }

BlockMemory::Action BlockMemory::Decide(const Point3 &point, bool rerouteOnly, uint32_t formId) const {
  return rerouteOnly || DetourTried(point) || Avoided(point, formId) ? Action::Reroute : Action::Search;
}

void BlockMemory::NoteDetour(const Point3 &point) {
  if (!DetourTried(point))
    m_detours.push_back(point);
}

bool BlockMemory::DetourTried(const Point3 &point) const {
  for (const Point3 &detour : m_detours)
    if (Distance2D(detour, point) <= kSameSpot)
      return true;
  return false;
}

bool BlockMemory::Avoid(const Point3 &point, float dirX, float dirY, uint32_t formId) {
  if (Avoided(point, formId))
    return false;
  if (m_avoided.size() >= kMaxAvoided)
    m_avoided.erase(m_avoided.begin());
  m_avoided.push_back({point, dirX, dirY, formId});
  return true;
}

bool BlockMemory::Avoided(const Point3 &point, uint32_t formId) const {
  for (const Spot &spot : m_avoided)
    if (Distance2D(spot.point, point) <= kSameSpot || (formId != 0 && spot.formId == formId))
      return true;
  return false;
}

bool HopBudget::Visited(const Point3 &place) const {
  for (const Point3 &earlier : m_places)
    if (Distance2D(earlier, place) <= kSamePlace)
      return true;
  return false;
}

void HopBudget::Begin(const Point3 &place, float targetDistance) {
  m_places.push_back(place);
  ++m_used;
  if (!m_hasBest || targetDistance < m_best) {
    m_best = targetDistance;
    m_hasBest = true;
  }
}

void HopBudget::Arrived(float targetDistance) {
  if (m_hasBest && targetDistance <= m_best - kBetterBy)
    m_used = 0;
  if (!m_hasBest || targetDistance < m_best) {
    m_best = targetDistance;
    m_hasBest = true;
  }
}

} // namespace PathProbe
