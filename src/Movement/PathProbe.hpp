#pragma once

// Engine-free part of the raycast path check (todo 48): sweeping a line over the ground with rays to see whether the
// player can walk it, generating and testing the points beside an obstacle, and the policy for keeping, dropping and
// rationing such detours. WalkerHandler supplies the rays through `World` and steers at the waypoint this picks; the
// same sweep is the primitive a navigation grid (todo 45) would be built from. Evidence, assumptions and the in-game
// checks are in docs/design/raycast-path-detours.md.
//
// Every number below is an estimate until it is checked in game (the note lists them). Distances are game units.

#include "Debug/OverlayMath.hpp"
#include <cstdint>
#include <vector>

namespace PathProbe {

using Overlay::Point3;

// What a ray ran into, as far as the detour logic cares.
enum class HitKind {
  World,  // terrain, a static, clutter: anything that is not one of the others
  Actor,  // an NPC or creature: they move, so they never block a route
  Door,   // doors are opened (Walker's recovery), not walked around
  Target, // the thing the walk is heading for: reaching it is the point
};

struct RayHit {
  bool hit = false;
  float fraction = 1.0f; // along from -> to
  HitKind kind = HitKind::World;
  uint32_t formId = 0; // the reference that was hit; 0 for terrain or unknown
  uint8_t layer = 0;   // its collision layer; 0 when unknown
  // The z of the surface normal at the hit: 1 for level ground, 0 for a wall. 1 when the engine gave no normal.
  float normalZ = 1.0f;
};

// The world as the probe sees it. The engine side casts Havok rays; tests use a few boxes on a floor.
class World {
public:
  virtual ~World() = default;
  virtual RayHit Cast(const Point3 &from, const Point3 &to) = 0;
  // Whether the player is allowed to be sent to this ground point (the engine side: on the navmesh).
  virtual bool Walkable(const Point3 &point) { return true; }
};

struct Settings {
  // The floor is sampled with a ray straight down every `groundStep`; the forward rays then run from one sample to
  // the one `spanSamples` later, at knee and chest height above the floor found at each end. Following the floor this
  // closely is what keeps a slope, a hump or a flight of stairs from reading as a wall.
  float groundStep = 20.0f;
  int spanSamples = 3;
  float kneeHeight = 40.0f;
  float chestHeight = 90.0f;
  // Sideways offset of the rays to either side of the centre line on a leg the planner invents, so a gap narrower
  // than the player counts as blocked: the radius of the player's collision capsule, which the engine side reads
  // (WorldUtils::PlayerCollisionShape; 20.3 units in game). There is no default: without the real radius the engine
  // side does not check at all. The path the pathfinder gave is checked on its centre line only: it is the navmesh's
  // own route, and a ray beside it would clip every door frame and corner it turns around.
  float halfWidth = 0.0f;
  // A rise between two floor samples above this is the top of something or a ledge, not a step or a slope; a fall
  // above `maxDrop` is an edge.
  float maxRise = 28.0f;
  float maxDrop = 60.0f;
  // The down ray starts this far above the last floor and ends this far below it.
  float floorAbove = 60.0f;
  float floorBelow = 120.0f;
  // A floor steeper than this is not walked on (the z of its normal; 0.6 is about 53 degrees). Stairs pass: their
  // treads are level, and the down ray never lands on a riser. An estimate: the engine's own limit was not read.
  float minFloorNormalZ = 0.6f;
  // The forward rays of the last span run this far past the end of the sweep, so the end point is not closer than
  // this to something in front of it. The navigation grid sets it to the player's radius: a cell whose centre is
  // nearer a wall than that cannot be stood in, and a ray that ends exactly on a face, or starts inside the thing
  // behind it, would otherwise let a route step through.
  float endClearance = 0.0f;
};

enum class Blocker {
  None,
  Obstacle, // a forward ray hit something
  Rise,     // the floor ahead is too high to step onto, or too steep to stand on
  Drop,     // no floor ahead, or too far down
  Door,     // a forward ray hit a door
};

const char *BlockerName(Blocker blocker);

// One ray of a sweep, for the debug overlay.
struct TraceRay {
  Point3 from;
  Point3 to; // the hit point when blocked
  bool blocked = false;
};

struct Trace {
  static constexpr size_t kMaxRays = 400;
  std::vector<TraceRay> rays;
  void Add(const Point3 &from, const Point3 &to, bool blocked);
};

struct SweepResult {
  Blocker blocker = Blocker::None;
  bool reachedTarget = false; // a ray touched the walk's target; the sweep stopped there and counts as clear
  float clearDistance = 0.0f; // horizontal distance from the start that is known to be free
  Point3 end;                 // the last floor point reached
  Point3 blockPoint;          // the ground point where it is blocked
  RayHit hit;                 // the blocking ray (Obstacle, Door) or the down ray (Rise)
  float step = 0.0f;          // Rise, Drop: the height change between the two floor samples (0 when no floor)
  float dirX = 0.0f;          // unit direction of the leg that was blocked
  float dirY = 0.0f;
  // Which of the side rays were blocked: +1 only the left one, -1 only the right one, 0 both or the centre line.
  int blockedSide = 0;
  int rays = 0;
  bool Clear() const { return blocker == Blocker::None; }
};

// Sweeps the straight line from the ground point `from` toward `to` (only its x and y matter: the floor is followed
// from `from.z`), at most `maxDistance` far. `halfWidth` 0 casts one ray per height on the centre line; above 0 it
// adds one that far to either side. Actors never block. A hit on the target ends the sweep as clear.
SweepResult Sweep(World &world, const Point3 &from, const Point3 &to, float maxDistance, const Settings &settings,
                  float halfWidth, Trace *trace = nullptr);

// The same over a polyline, the floor carried from leg to leg. `points[0]` is where the player stands.
SweepResult SweepPath(World &world, const std::vector<Point3> &points, float maxDistance, const Settings &settings,
                      float halfWidth, Trace *trace = nullptr);

// --- Polyline helpers -------------------------------------------------------------------------------------------

float PathLength(const std::vector<Point3> &points);
// The point `distance` along the polyline, clamped to its ends.
Point3 PointAlong(const std::vector<Point3> &points, float distance);
// Cuts `trim` off the end of the polyline (the walk ends within reach of its target, whose own collision would
// otherwise read as an obstacle). Never shorter than its first point.
std::vector<Point3> TrimEnd(std::vector<Point3> points, float trim);

// --- Detour planning --------------------------------------------------------------------------------------------

enum class CandidateStatus {
  Pending,
  FirstLegBlocked, // the line from the player to the point is not free
  NotWalkable,     // the point is not on the navmesh
  OnwardBlocked,   // no free line from the point back onto the path
  Valid,
};

struct Candidate {
  Point3 point; // on the floor once the first leg was swept
  int side = 0; // +1 left of the blocked direction, -1 right
  float lateral = 0.0f;
  float cost = 0.0f;
  Point3 rejoin; // Valid: where the free onward line meets the path again
  CandidateStatus status = CandidateStatus::Pending;
};

// Points to either side of a blocked spot: perpendicular to the blocked direction, a little back from the hit, at
// growing distances. Sorted by cost: the sideways distance, plus a penalty for the side that is not preferred.
// `preferredSide` is the side a detour on this route already took (strong penalty, so the route does not flip between
// left and right); `hintSide` the side the sweep suggests (only one of the two parallel rays was blocked). 0 for none.
std::vector<Candidate> MakeCandidates(const Point3 &blockPoint, float dirX, float dirY, int preferredSide,
                                      int hintSide);

inline constexpr float kSidePenalty = 250.0f; // other side than a detour already taken on this route
inline constexpr float kHintPenalty = 60.0f;  // other side than the sweep suggests

// Tests candidates one by one until one has a free line from the player to it and a free line from it back onto the
// path beyond the obstacle. The work is cut into calls with a ray budget, so a search never costs more than that in
// one frame.
class DetourPlanner {
public:
  // How far beyond the blocked spot the onward line may meet the path again; the nearer one is tried first.
  static constexpr float kRejoinNear = 350.0f;
  static constexpr float kRejoinFar = 550.0f;

  enum class State { Idle, Working, Found, Failed };

  // `path` starts at the player and is already trimmed (TrimEnd); `blocked` is the sweep that found the obstacle on
  // it.
  void Start(std::vector<Point3> path, const SweepResult &blocked, int preferredSide);
  void Reset();
  // Carries on from where the last call stopped; `player` is where the player stands now. Stops once `rayBudget` rays
  // were cast in this call (a sweep is never cut short, so it can overshoot by one sweep).
  State Step(World &world, const Point3 &player, const Settings &settings, int rayBudget, Trace *trace = nullptr);
  State GetState() const { return m_state; }
  const Candidate &Result() const { return m_candidates[m_index]; } // Found only
  const std::vector<Candidate> &Candidates() const { return m_candidates; }
  int Rays() const { return m_rays; }

private:
  State m_state = State::Idle;
  std::vector<Point3> m_path;
  std::vector<Candidate> m_candidates;
  float m_blockDistance = 0.0f;
  size_t m_index = 0;
  int m_stage = 0; // 0: first leg; 1, 2: onward line to the near / far rejoin point
  int m_rays = 0;
};

// --- Detour policy ----------------------------------------------------------------------------------------------

// Keeps the waypoint of a running detour and decides when it is over, which side later detours on the same route
// prefer, and how many detours a route may take. Like the recovery budget (WalkerLogic::RecoveryPlanner), the budget
// only comes back with real progress toward the target: a walk that keeps side-stepping in one place must not go on
// for ever.
class DetourTracker {
public:
  static constexpr int kBudget = 6;
  static constexpr float kRefillProgress = 350.0f; // 5 meters closer than the best spot a detour started from
  static constexpr float kArriveDistance = 45.0f;
  static constexpr float kMaxSeconds = 8.0f;
  static constexpr float kFailCooldown = 1.5f; // after a search found nothing, before the next one

  enum class End {
    Arrived,
    PathClear,  // the path is free again from where the player now stands
    LegBlocked, // something got into the line to the waypoint
    TimedOut,
    Abandoned, // recovery, a new route or a stop took over
  };
  static const char *EndName(End end);

  // A new route (new target, a load door): forget everything.
  void Reset();
  // Every frame the walker moves, with the distance still to go to the target.
  void Update(float dt, float remaining);
  // Whether a new search may start: no detour running, budget left, not cooling down after a failed search.
  bool MayPlan() const { return !m_active && m_used < kBudget && m_cooldown <= 0.0f; }
  void Begin(const Point3 &waypoint, int side, float remaining);
  // A detour of several waypoints walked in order (a route from the navigation grid). `waypoints` must not be empty.
  void Begin(std::vector<Point3> waypoints, int side, float remaining);
  void PlanFailed() { m_cooldown = kFailCooldown; }
  void Finish();

  bool Active() const { return m_active; }
  // The waypoint being walked to.
  const Point3 &Waypoint() const { return m_waypoints[m_index]; }
  // The waypoints still to come, the current one first.
  std::vector<Point3> Remaining() const;
  // Skips `count` waypoints (the one after them can be walked to directly). Never past the last.
  void SkipAhead(size_t count);
  int PreferredSide() const { return m_side; }
  int Used() const { return m_used; }
  // While a detour runs: true when the player is at the last waypoint (End::Arrived) or the current one took too long
  // (End::TimedOut). Reaching a waypoint that is not the last moves on to the next and is not an end.
  bool Expired(float playerX, float playerY, End &why);

private:
  bool m_active = false;
  std::vector<Point3> m_waypoints = {Point3{}};
  size_t m_index = 0;
  int m_side = 0;
  int m_used = 0;
  float m_elapsed = 0.0f;
  float m_cooldown = 0.0f;
  float m_bestStart = 0.0f; // smallest remaining distance at which a detour started
  bool m_hasStart = false;
};

// --- Blocked spots ----------------------------------------------------------------------------------------------

// What a walk has learnt about the places where its route was blocked. A way round on foot is tried once per
// obstacle. If the route is blocked there again afterwards, or no way round exists, or the player gets stuck at it,
// the pathfinder itself has to find another route (the cars in Primm fill the only way through; the next way in is
// over a bridge far off): the engine side then asks it again and again, marking the navmesh it must avoid.
class BlockMemory {
public:
  static constexpr float kSameSpot = 150.0f; // two blocks this close are the same obstacle
  static constexpr size_t kMaxAvoided = 6;

  struct Spot {
    Point3 point;
    float dirX = 0.0f; // the direction the route ran into it
    float dirY = 0.0f;
    uint32_t formId = 0; // the reference that blocked, 0 for terrain or unknown
  };

  enum class Action {
    Search,  // look for a way round on foot (the grid, or a side-step)
    Reroute, // that was tried for this obstacle already: ask the pathfinder for another route
  };

  // Everything (another interior or worldspace, a load).
  void Reset();
  // A new walk: side-steps may be tried again, the avoided spots stay. She passes the same car on the next walk too,
  // and should not have to bump into it again.
  void ResetWalk() { m_detours.clear(); }
  // `rerouteOnly` skips the side-step (console mode for testing the rerouting alone). `formId` is the reference
  // that blocks (0 for none): the same object seen from another side is the same obstacle.
  Action Decide(const Point3 &point, bool rerouteOnly, uint32_t formId = 0) const;
  // A side-step was started for a block at `point`.
  void NoteDetour(const Point3 &point);
  bool DetourTried(const Point3 &point) const;
  // Marks the spot as avoided, dropping the oldest one when the list is full. False when it already is.
  bool Avoid(const Point3 &point, float dirX, float dirY, uint32_t formId = 0);
  bool Avoided(const Point3 &point, uint32_t formId = 0) const;
  const std::vector<Spot> &AvoidedSpots() const { return m_avoided; }

private:
  std::vector<Point3> m_detours;
  std::vector<Spot> m_avoided;
};

} // namespace PathProbe
