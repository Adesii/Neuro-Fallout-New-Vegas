#include "WalkerHandler.hpp"
#include "Context/Distance.hpp"
#include "Debug/WalkerOverlay.hpp"
#include "Debug/WalkerProbe.hpp"
#include "GameData.h"
#include "GameOSDepend.h"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "Movement/AimLogic.hpp"
#include "Movement/NavGrid.hpp"
#include "Movement/PathProbe.hpp"
#include "Movement/RouteDebug.hpp"
#include "Movement/WalkerLogic.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "decoding.h"
#include "defs/Player.h"
#include "hooks/Hooks_DirectInput8Create.h"
#include "itr/PathingCommands.h"
#include "nvse/GameAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameUI.h"
#include "utils/AimUtils.hpp"
#include "utils/InputUtils.hpp"
#include "utils/TimeUtils.hpp"
#include "utils/WorldUtils.hpp"
#include "utils/math.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <utility>

namespace Walker {
namespace {

using WorldUtils::Distance3D;
using WorldUtils::LookupReference;

constexpr float kInteractionDistance = 100.0f;
// Actors that are the object of an interaction (a quest NPC to talk to) walk around, so waiting for a 100-unit approach
// and a settled view never succeeds. Within this range she stops and interacts at once; once engaged the range grows
// so a step back by the NPC does not cancel it. The distances are estimates: whether the game accepts an activation
// from this far is unverified. When to activate once within reach is WalkerLogic::InteractionGate.
constexpr float kActorInteractionDistance = 150.0f;
constexpr float kActorInteractionRelease = 250.0f;
// A plain move within reach but without a clear ray or the same level this long fails with "not reachable from here".
constexpr float kMoveBlockedGiveUpDelay = WalkerLogic::InteractionGate::kGiveUpSeconds;
// The engine path search is expensive, so the next waypoint is reused between these refreshes.
constexpr float kRepathInterval = 0.25f;
constexpr float kRepathMinInterval = 0.1f; // floor for the early refresh when the cached waypoint is reached
constexpr float kWaypointReachedDistance = 60.0f;
constexpr float kDestinationMoveTolerance = 50.0f;
constexpr float kCloseLookDistance = 400.0f;
constexpr float kLookAlignmentTolerance = 0.12f;
constexpr float kLookTargetResponse = 4.0f;
// Walker's smoothed turn; combat's snap-to-target turn lives in AimUtils.cpp (kCombatTurnRate).
constexpr float kWalkTurnRate = 3.0f;
constexpr float kTurnSpeedResponse = 4.0f;
constexpr float kTurnAccelerationResponse = 7.0f;
constexpr float kTurnReversalResponse = 2.5f;
constexpr float kTurnDeadZone = 0.01f;
constexpr UINT8 kUnboundKey = 0xFF;

// Stuck detection is ignored this long after the player moves into another cell (a load or a door).
constexpr float kCellChangeGrace = 5.0f;

// Interaction line of sight: the ray is trimmed less than combat's (35/60) because the player stands within a metre.
constexpr float kInteractionRayStart = 15.0f;
constexpr float kInteractionRayEnd = 25.0f;
constexpr float kLineOfSightInterval = 0.1f;
// A plain move counts as reached without a clear ray when the target is at about the same height.
constexpr float kSameLevelHeight = 60.0f;

// Doors considered during recovery: a closed one this close and within roughly 57 degrees of the facing (Skyrim's
// walker uses 200 units too). A locked one this close and nearly dead ahead fails the walk at once.
constexpr float kDoorSearchDistance = 200.0f;
constexpr float kDoorMaxHeightDifference = 150.0f;
constexpr float kDoorSearchCone = 1.0f;
constexpr float kDoorBlockingDistance = 130.0f;
constexpr float kDoorBlockingCone = 0.5f;
constexpr float kDoorFaceTolerance = 0.15f;
constexpr float kDoorFaceTimeout = 1.0f;
constexpr float kDoorOpenWait = 0.8f;

constexpr float kJumpPulseSeconds = 0.12f;
constexpr float kSecondJumpAt = 0.6f;
// Wander: a random navmesh point this close to where the player stands is a real change of angle; the point it
// snapped to must be near the one asked for, and the step is over once the player is this close to it.
constexpr int kDetourCandidates = 10;
constexpr float kDetourSnapTolerance = 120.0f;
constexpr float kDetourMinDistance = 120.0f;
constexpr float kDetourArrivedDistance = 80.0f;
constexpr float kQuarterTurn = 1.5707964f;
constexpr float kHalfTurn = 3.1415927f;

// Raycast path check (todo 48, docs/design/raycast-path-detours.md). The path is swept this far ahead on every route
// search; a search for a way round casts at most this many rays a frame. To call the path clear again while a detour
// runs, the side rays are this much further out than the player's half width, so the detour is not dropped the moment
// the centre line is free by a hair. An obstacle this close to the end of the path is the target's own surroundings,
// which arriving and the interaction checks deal with.
constexpr float kProbeLookAhead = 400.0f;
constexpr int kProbeRayBudget = 300;
constexpr float kProbeClearWidthFactor = 1.6f;
constexpr float kProbeMinOnward = 60.0f;
constexpr float kProbeNoLimit = 1.0e9f;

// Rerouting (the pathfinder's avoid nodes, see Pathing::BuildPath). A blocked spot the Walker cannot get past is given
// to the pathfinder as the navmesh triangles the obstacle stands on: every triangle that overlaps the bounds of the
// object that blocks, or a circle around the blocked spot when it has no usable bounds (terrain, a building). One
// triangle is not enough: in Primm the pathfinder threaded through the next triangle of the same car (2026-11-10).
// Each triangle gets a sphere on its centre, which is the one point the search tests. The cost multiplies the
// triangle's length in the search by about a thousand: the way through stays possible when it is the only one, and
// loses to any other way within a few kilometres. All the numbers are estimates.
constexpr float kAvoidCircleRadius = 150.0f; // around a blocked spot without usable bounds,
constexpr float kAvoidCircleAhead = 60.0f;   // centred this far past it along the route
constexpr float kAvoidMaxBound = 800.0f;     // larger bounds are a building or a piece of land, not an obstacle
constexpr float kAvoidRadius = 100.0f;       // of the sphere on each triangle centre
constexpr float kAvoidLooseRadius = 350.0f;  // of the one sphere used when no triangle is found
constexpr float kAvoidCost = 1000.0f;
constexpr float kAvoidHeightTolerance = 150.0f;
constexpr float kAvoidSameCentre = 30.0f;
constexpr size_t kAvoidPerSpot = 16;
constexpr size_t kAvoidMaxSpheres = 64;

// The navigation grid (todo 45, src/Movement/NavGrid.*), the normal way round a blocked route. It is searched for a
// way from where she stands to the point of the route this far beyond the blocked spot; at most this many rays a
// frame (5 ms at the measured 5 microseconds a ray). A search that does not reach that point is still
// walked when it ends this much nearer to it than the blocked spot is.
constexpr float kGridRejoin = 450.0f;
constexpr int kGridRayBudget = 1000;
constexpr float kGridPartialGain = 100.0f;
// Getting stuck this close to a spot the path check saw blocked means that spot is the reason. Stuck anywhere else
// for the third time in a row, the place right ahead of her is given to the pathfinder instead.
constexpr float kStuckAtBlockDistance = 250.0f;
constexpr int kStuckAttemptsBeforeAvoid = 2;

// GetOpenState(ref) is FalloutNV.exe 1.4.0.525 0x47B250 (__cdecl). Two sources agree on the address and on the result
// values None 0 / Open 1 / Opening 2 / Closed 3 / Closing 4: itr-nvse features/DoorPinchFix.cpp (MIT, revision in
// tools/reference-manifest.json) and JohnnyGuitarNVSE BGSOpenCloseForm.
constexpr uint32_t kOpenStateClosed = 3;
using GetOpenStateFn = uint32_t(__cdecl *)(TESObjectREFR *);
const auto kGetOpenState = reinterpret_cast<GetOpenStateFn>(0x47B250);

// Just enough of ExtraLock to read the locked flag: BSExtraData is 0xC bytes and `data` follows it; the data block has
// the key at 0x04 and `flags` at 0x08 with bit 1 = locked. JIP-LN-NVSE GameExtraData.h and NVSE GameExtraData.h agree
// on those offsets; only JIP documents the bit. The width of the lock level at 0x00 differs between the two, so it is
// not read. A door without an ExtraLock is not locked.
struct LockData {
  uint8_t head[8];
  uint8_t flags;
};
struct ExtraLockView {
  BSExtraData base;
  LockData *data;
};
static_assert(sizeof(BSExtraData) == 0xC, "ExtraLock::data follows the 0xC-byte BSExtraData");

struct Command {
  uint32_t targetFormId = 0;
  Intent intent = Intent::Move;
  Owner owner = Owner::None;
  std::string description;
};

// The recovery step being carried out after the player was judged stuck.
struct Recovery {
  bool active = false;
  WalkerLogic::RecoveryStep step = WalkerLogic::RecoveryStep::Sidestep;
  bool mirrored = false;
  float elapsed = 0.0f;
  int jumps = 0;
  uint32_t doorFormId = 0;
  bool doorActivated = false;
  float doorActivatedAt = 0.0f;
  bool hasDetour = false;
  NiPoint3 detour = {0.0f, 0.0f, 0.0f};
};

// What the raycast path check cost over one command, for the log.
struct ProbeCost {
  int frames = 0; // frames in which rays were cast
  int rays = 0;
  double microseconds = 0.0;
  double worstFrame = 0.0;
  void Add(int castRays, double took) {
    if (castRays <= 0)
      return;
    ++frames;
    rays += castRays;
    microseconds += took;
    worstFrame = std::max(worstFrame, took);
  }
};

struct WalkerState {
  TESObjectREFR *waypointMarker = nullptr;
  TESObjectREFR *trackedTarget = nullptr;
  TESObjectCELL *trackedCell = nullptr;
  NiPoint3 smoothedLookTarget = {0.0f, 0.0f, 0.0f};
  NiPoint3 lastPlayerPosition = {0.0f, 0.0f, 0.0f};
  WalkerLogic::InteractionGate interaction;
  float blockedTime = 0.0f;
  float navigationAge = 0.0f;
  float lineOfSightTimer = 0.0f;
  float turnVelocity = 0.0f;
  bool lookTargetInitialized = false;
  bool positionInitialized = false;
  bool navigationValid = false;
  bool lineOfSight = false;
  bool engaged = false;        // an actor target is within interaction range
  bool loggedSnapped = false;  // the route fell back to the navmesh point next to the destination (logged once)
  bool loggedStraight = false; // the route fell back to a straight line (logged once)
  PathPoint3 navigationPoint{};
  NiPoint3 navigationDestination = {0.0f, 0.0f, 0.0f};
  WalkerLogic::StuckDetector stuck;
  WalkerLogic::StuckDetector detourStuck; // a Wander step that goes nowhere ends early
  WalkerLogic::OscillationDetector oscillation;
  WalkerLogic::SlideDetector slide; // running into an obstacle at an angle
  WalkerLogic::ProgressTracker progress;
  WalkerLogic::RecoveryPlanner planner;
  Recovery recovery;
  // Raycast path check: the pathfinder's nodes of the last route search (empty when it found none), the one steered
  // at, whether the route was searched this frame, and the detour being planned or walked.
  std::vector<PathPoint3> routeNodes;
  size_t routeChosen = 0;
  bool routeRefreshed = false;
  PathProbe::DetourTracker detour;
  PathProbe::DetourPlanner detourPlanner;
  PathProbe::Point3 detourRejoin;
  std::string detourBlocker;   // what blocked the path, for the log
  bool loggedNoDetour = false; // "no way round" is logged once per route
  bool loggedNoShape = false;  // so is an unreadable collision shape
  ProbeCost probeCost;
  // The blocked spot a search for a side-step is running for, and the last one the path check saw.
  PathProbe::BlockMemory::Spot searchBlock;
  PathProbe::BlockMemory::Spot lastBlock;
  bool hasLastBlock = false;
  // Length of the pathfinder's route from the player at the last search (-1: it found none), and the length before
  // the last spot was given to it to avoid (to say what the rerouting did).
  float routeLength = -1.0f;
  bool reportReroute = false;
  float lengthBeforeReroute = -1.0f;
  bool progressAlongRoute = false; // what the progress stall last measured: the route's length or the straight line
  // A search on the navigation grid for a way round `searchBlock`, its goal, and whether the detour being walked
  // came from the grid.
  NavGrid::Search gridSearch;
  PathProbe::Point3 gridGoal;
  bool detourFromGrid = false;
  std::string lockedDoor; // a locked closed door seen in front of the player while stuck
  // Per command, not per route: they carry on across a cell change.
  WalkerLogic::ProgressReminder reminder;
  WalkerLogic::PauseNotice pauseNotice;
  bool hasCommand = false;
  Command command;
  UINT8 heldMovementKeys[4] = {kUnboundKey, kUnboundKey, kUnboundKey, kUnboundKey};
};

WalkerState g_state;

// Blocked spots outlive a walk: she passes the same car on the next one. They belong to one interior or worldspace
// (`g_avoidSpace`, a form ID) and are forgotten when she leaves it or a save is loaded.
PathProbe::BlockMemory g_blocks;
std::vector<Pathing::AvoidSphere> g_avoid;
uint32_t g_avoidSpace = 0;
// What the navigation grid has sampled there. It is forgotten with the blocked spots.
NavGrid::Grid g_grid;

void ForgetSpace() {
  g_state.gridSearch.Reset(); // it points into the grid
  g_grid.Clear();
  g_blocks.Reset();
  g_avoid.clear();
}

// Debug mode (todo 43): what the last route search steered by and where the player went. Kept apart from WalkerState
// so it outlives the command: a failed walk is exactly when the route needs looking at. Filled only while
// WalkerOverlay::Enabled(); it never feeds back into steering.
struct DebugCapture {
  RouteDebug::View view;
  RouteDebug::Trail trail;
  bool hasLogged = false;
  RouteDebug::Route logged;
  RouteDebug::Mode loggedMode = RouteDebug::Mode::None;
  bool wasEnabled = false;
};
DebugCapture g_debug;
std::vector<Event> g_events;
bool g_lookSuppressed = false;
InputUtils::PulseQueue g_jumpPulses{InputCore::Owner::WalkerPulse};

using AimUtils::EffectiveYaw;
using AimUtils::SetPitch;
using AimUtils::SetYaw;

bool IsInteractable(TESObjectREFR *ref) { return WorldUtils::ClassifyObject(ref).interactable; }

// Game time stands still in menus, so the paused-command notice runs on the wall clock.
float RealFrameDelta() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point last{};
  const auto now = Clock::now();
  const float seconds = last == Clock::time_point{} ? 0.0f : std::chrono::duration<float>(now - last).count();
  last = now;
  return seconds > 0.5f ? 0.0f : std::clamp(seconds, 0.0f, 0.25f);
}

bool IsValidPoint(const PathPoint3 &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

void ReleaseMovementKeys() {
  auto &input = DIHookControl::GetSingleton();
  for (UINT8 &key : g_state.heldMovementKeys) {
    if (key != kUnboundKey)
      input.SetKeyHeldState(InputCore::Owner::Walker, key, false);
    key = kUnboundKey;
  }
}

int RetryBudget() {
  return g_state.command.owner == Owner::Quest ? WalkerLogic::RecoveryPlanner::kQuestBudget
                                               : WalkerLogic::RecoveryPlanner::kDefaultBudget;
}

void ResetRouteState() {
  g_jumpPulses.ReleaseAll();
  g_state.trackedTarget = nullptr;
  g_state.trackedCell = nullptr;
  g_state.interaction.Reset();
  g_state.blockedTime = 0.0f;
  g_state.lineOfSightTimer = 0.0f;
  g_state.lineOfSight = false;
  g_state.engaged = false;
  g_state.loggedSnapped = false;
  g_state.loggedStraight = false;
  g_state.navigationValid = false;
  g_state.navigationAge = 0.0f;
  g_state.turnVelocity = 0.0f;
  g_state.lookTargetInitialized = false;
  g_state.positionInitialized = false;
  g_state.stuck.Reset();
  g_state.detourStuck.Reset();
  g_state.oscillation.Reset();
  g_state.slide.Reset();
  g_state.progress.Reset();
  g_state.planner.Reset(RetryBudget());
  g_state.recovery = {};
  g_state.lockedDoor.clear();
  g_state.routeNodes.clear();
  g_state.routeChosen = 0;
  g_state.routeRefreshed = false;
  g_state.detour.Reset();
  g_state.detourPlanner.Reset();
  g_state.detourBlocker.clear();
  g_state.loggedNoDetour = false;
  g_state.loggedNoShape = false;
  g_state.searchBlock = {};
  g_state.lastBlock = {};
  g_state.hasLastBlock = false;
  g_state.routeLength = -1.0f;
  g_state.reportReroute = false;
  g_state.progressAlongRoute = false;
  g_state.gridSearch.Reset();
  g_state.detourFromGrid = false;
}

// RouteDebug::Point3 and PathProbe::Point3 are the same type (Overlay::Point3).
RouteDebug::Point3 ToDebugPoint(const NiPoint3 &p) { return {p.x, p.y, p.z}; }
RouteDebug::Point3 ToDebugPoint(const PathPoint3 &p) { return {p.x, p.y, p.z}; }

void ResetDebugCapture() {
  const bool wasEnabled = g_debug.wasEnabled;
  g_debug = {};
  g_debug.wasEnabled = wasEnabled;
}

void PublishDebug(bool active) {
  if (!WalkerOverlay::Enabled() || !g_debug.view.hasRoute)
    return;
  g_debug.view.active = active;
  g_debug.view.trail = g_debug.trail.Points();
  g_debug.view.probe.route = g_state.detourFromGrid ? g_state.detour.Remaining() : std::vector<PathProbe::Point3>{};
  g_debug.view.probe.avoided.clear();
  for (const Pathing::AvoidSphere &sphere : g_avoid)
    g_debug.view.probe.avoided.push_back({ToDebugPoint(sphere.centre), sphere.radius});
  WalkerOverlay::Publish(g_debug.view);
}

void ClearCommand(PlayerCharacter *player) {
  if (g_state.hasCommand)
    PublishDebug(false); // the route stays on screen, dimmed, after the walk ends
  if (g_state.probeCost.frames > 0)
    _MESSAGE("Walker path check on the walk to %s: %d rays in %d frames, %.2f ms in all, %.0f microseconds in the "
             "worst frame",
             g_state.command.description.c_str(), g_state.probeCost.rays, g_state.probeCost.frames,
             g_state.probeCost.microseconds / 1000.0, g_state.probeCost.worstFrame);
  g_state.probeCost = {};
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
  ResetRouteState();
  g_blocks.ResetWalk(); // the next walk may side-step again; the spots the pathfinder avoids stay
  g_state.reminder.Reset();
  g_state.pauseNotice.Reset();
  g_state.hasCommand = false;
  g_state.command = {};
}

Event MakeEvent(EventType type) {
  Event event;
  event.type = type;
  event.owner = g_state.command.owner;
  event.targetFormId = g_state.command.targetFormId;
  event.description = g_state.command.description;
  return event;
}

void FinishCompleted(PlayerCharacter *player, std::string message) {
  Event event = MakeEvent(EventType::Completed);
  event.message = std::move(message);
  g_events.push_back(std::move(event));
  ClearCommand(player);
}

void FinishFailed(PlayerCharacter *player, WalkerLogic::FailureReason reason, std::string detail = {}) {
  Event event = MakeEvent(EventType::Failed);
  event.reason = reason;
  event.detail = std::move(detail);
  _WARNING("Walker failed %s (reason %d)", g_state.command.description.c_str(), static_cast<int>(reason));
  g_events.push_back(std::move(event));
  ClearCommand(player);
}

bool EnsureWaypointMarker(PlayerCharacter *player) {
  if (!g_state.waypointMarker)
    g_state.waypointMarker = TESObjectREFR::Create(true);
  if (!g_state.waypointMarker)
    return false;
  g_state.waypointMarker->parentCell = player->GetParentCell();
  return true;
}

TESObjectCELL *GetNavigationCell(PlayerCharacter *player) {
  auto *cell = TES::GetSingleton()->currentInterior;
  return cell ? cell : player->GetParentCell();
}

// The first path node past the waypoint-reached radius, so a node the player is already standing on does not become
// the steering target (and force a repath every frame). The last node when every one is that close.
// `debug` (debug mode only) receives the nodes and the chosen index. `keepRoute` stores them as the walk's route for
// the raycast path check; the Wander step's reachability tests leave it false.
bool BuildPathPoint(PlayerCharacter *player, const NiPoint3 &destination, PathPoint3 &pathPoint,
                    RouteDebug::Route *debug = nullptr, bool keepRoute = false) {
  g_state.waypointMarker->pos = destination;
  Pathing::PathResult path;
  if (!Pathing::BuildPath(player, g_state.waypointMarker, path, g_avoid.empty() ? nullptr : &g_avoid) ||
      path.nodes.empty())
    return false;
  size_t chosen = path.nodes.size() - 1;
  for (size_t index = 1; index < path.nodes.size(); ++index) {
    const PathPoint3 &node = path.nodes[index];
    if (Math::GetDistance2D(node, player->pos) > kWaypointReachedDistance) {
      chosen = index;
      break;
    }
  }
  pathPoint = path.nodes[chosen];
  if (keepRoute) {
    g_state.routeNodes = path.nodes;
    g_state.routeChosen = chosen;
    g_state.routeLength = path.distance;
  }
  if (debug) {
    debug->nodes.clear();
    debug->nodes.reserve(path.nodes.size());
    for (const PathPoint3 &node : path.nodes)
      debug->nodes.push_back(ToDebugPoint(node));
    debug->chosen = static_cast<int>(chosen);
  }
  return IsValidPoint(pathPoint);
}

// `debug` (debug mode only) records how the point was found. The two fallbacks are logged once per route either way.
PathPoint3 ResolveNavigationPoint(PlayerCharacter *player, const NiPoint3 &destination, RouteDebug::Route *debug) {
  using RouteDebug::Mode;
  g_state.routeNodes.clear(); // stays empty unless a path is found: the check then sweeps the straight line
  g_state.routeChosen = 0;
  g_state.routeLength = -1.0f;
  if (Distance3D(player->GetPos(), destination) < kInteractionDistance) {
    if (debug)
      debug->mode = Mode::Close;
    return {destination.x, destination.y, destination.z};
  }

  PathPoint3 pathPoint;
  if (BuildPathPoint(player, destination, pathPoint, debug, true)) {
    if (debug)
      debug->mode = Mode::Path;
    return pathPoint;
  }

  auto *cell = GetNavigationCell(player);
  bool snapped = false;
  NiPoint3 onNavmesh = {0.0f, 0.0f, 0.0f};
  if (cell) {
    NiPoint4 closest = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    Pathing::GetClosestNavMeshTriangle(cell, destination, false, 0.0f, closest);
    if (closest.x != FLT_MAX) {
      snapped = true;
      onNavmesh = {closest.x, closest.y, closest.z};
      if (debug) {
        debug->hasSnapped = true;
        debug->snapped = ToDebugPoint(onNavmesh);
      }
      if (BuildPathPoint(player, onNavmesh, pathPoint, debug, true)) {
        if (debug)
          debug->mode = Mode::Snapped;
        if (!g_state.loggedSnapped) {
          g_state.loggedSnapped = true;
          _MESSAGE("Walker found no path to %s itself; heading for the closest navmesh point (%.0f, %.0f, %.0f)",
                   g_state.command.description.c_str(), onNavmesh.x, onNavmesh.y, onNavmesh.z);
        }
        return pathPoint;
      }
    }
  }
  if (debug) {
    debug->mode = Mode::Straight;
    debug->nodes.clear();
    debug->chosen = -1;
  }
  if (!g_state.loggedStraight) {
    g_state.loggedStraight = true;
    if (snapped)
      _WARNING("Walker found no path to %s, nor to the closest navmesh point (%.0f, %.0f, %.0f); steering in a "
               "straight line at (%.0f, %.0f, %.0f)",
               g_state.command.description.c_str(), onNavmesh.x, onNavmesh.y, onNavmesh.z, destination.x, destination.y,
               destination.z);
    else
      _WARNING("Walker found no path to %s and no navmesh near it%s; steering in a straight line at (%.0f, %.0f, "
               "%.0f)",
               g_state.command.description.c_str(), cell ? "" : " (no navigation cell)", destination.x, destination.y,
               destination.z);
  }
  return {destination.x, destination.y, destination.z};
}

// Debug mode: fills in where the search ran from and for, logs the route when it differs from the last one logged,
// and keeps it for the overlay.
void RecordRoute(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &destination, RouteDebug::Route route,
                 const PathPoint3 &steer) {
  const NiPoint3 here = player->GetPos();
  route.steer = ToDebugPoint(steer);
  route.destination = ToDebugPoint(destination);
  route.detour = g_state.recovery.hasDetour;
  route.start = ToDebugPoint(here);
  auto *searchCell = GetNavigationCell(player);
  auto *playerCell = player->GetParentCell();
  route.searchCell = searchCell ? searchCell->GetFormID() : 0;
  route.playerCell = playerCell ? playerCell->GetFormID() : 0;
  route.markerCell = g_state.waypointMarker && g_state.waypointMarker->parentCell
                         ? g_state.waypointMarker->parentCell->GetFormID()
                         : 0;
  route.targetCell = target && target->parentCell ? target->parentCell->GetFormID() : 0;
  // Within reach the check is skipped: it would run every frame and says nothing about the route.
  if (route.mode != RouteDebug::Mode::Close && searchCell)
    route.playerOnNavmesh = Pathing::PointOnNavmesh({here.x, here.y, here.z}, searchCell, searchCell->worldSpace);

  if (WalkerOverlay::Logs(WalkerOverlay::LogLevel::Routes) &&
      (!g_debug.hasLogged || !RouteDebug::SameRoute(g_debug.logged, route))) {
    _MESSAGE("Walker debug route to %s: %s", g_state.command.description.c_str(),
             RouteDebug::Describe(route, route.start).c_str());
    if (!g_debug.hasLogged || route.mode != g_debug.loggedMode)
      Console_Print("Walker debug: %s", RouteDebug::ModeName(route.mode));
    g_debug.logged = route;
    g_debug.loggedMode = route.mode;
    g_debug.hasLogged = true;
  }
  g_debug.view.route = std::move(route);
  g_debug.view.hasRoute = true;
}

// Steers at the target, or at the Wander spot while a Wander recovery step is running.
PathPoint3 GetNavigationPoint(PlayerCharacter *player, TESObjectREFR *target, float deltaTime) {
  const NiPoint3 destination = g_state.recovery.hasDetour ? g_state.recovery.detour : target->GetPos();
  const NiPoint3 playerPosition = player->GetPos();
  if (Distance3D(playerPosition, destination) < kInteractionDistance) {
    g_state.navigationValid = false;
    g_state.routeNodes.clear();
    const PathPoint3 direct = {destination.x, destination.y, destination.z};
    if (WalkerOverlay::Enabled()) {
      RouteDebug::Route route;
      route.mode = RouteDebug::Mode::Close;
      RecordRoute(player, target, destination, std::move(route), direct);
    }
    return direct;
  }

  g_state.navigationAge += deltaTime;
  bool refresh = !g_state.navigationValid || g_state.navigationAge >= kRepathInterval ||
                 Distance3D(destination, g_state.navigationDestination) > kDestinationMoveTolerance;
  if (!refresh && g_state.navigationAge >= kRepathMinInterval) {
    refresh = Math::GetDistance2D(playerPosition, g_state.navigationPoint) < kWaypointReachedDistance;
  }
  if (refresh) {
    RouteDebug::Route route;
    RouteDebug::Route *debug = WalkerOverlay::Enabled() ? &route : nullptr;
    g_state.navigationPoint = ResolveNavigationPoint(player, destination, debug);
    g_state.navigationDestination = destination;
    g_state.navigationAge = 0.0f;
    g_state.navigationValid = true;
    g_state.routeRefreshed = true;
    if (g_state.reportReroute) {
      // What giving the pathfinder a spot to avoid did to the route. The other way round is longer and may lead away
      // from the target for a while, so progress starts over from the new route's length (see Process).
      g_state.reportReroute = false;
      g_state.progress.Reset();
      const float before = g_state.lengthBeforeReroute;
      const float now = g_state.routeLength;
      if (now < 0.0f)
        _WARNING("Walker reroute: the pathfinder finds no route to %s any more", g_state.command.description.c_str());
      else if (before >= 0.0f && std::abs(now - before) < 70.0f)
        _MESSAGE("Walker reroute: the route to %s is still %.0f m long: the pathfinder has no other way, or the "
                 "avoided spot does not reach it",
                 g_state.command.description.c_str(), Units::ToMeters(now));
      else
        _MESSAGE("Walker reroute: the route to %s is now %.0f m long (was %.0f m)", g_state.command.description.c_str(),
                 Units::ToMeters(now), Units::ToMeters(std::max(before, 0.0f)));
    }
    if (debug)
      RecordRoute(player, target, destination, std::move(route), g_state.navigationPoint);
  }
  return g_state.navigationPoint;
}

// --- Doors ----------------------------------------------------------------------------------------------------

std::string DoorLabel(TESObjectREFR *door) {
  const char *name = door ? door->GetFullName() : nullptr;
  return name && name[0] ? std::string("the door \"") + name + "\"" : "a door";
}

bool IsLocked(TESObjectREFR *ref) {
  auto *extra = static_cast<ExtraLockView *>(
      static_cast<void *>(ref->extraDataList.GetExtraData(static_cast<uint8_t>(_ExtraDataType::ExtraLock))));
  return extra && extra->data && (extra->data->flags & 1) != 0;
}

struct DoorAhead {
  TESObjectREFR *ref = nullptr;
  bool closed = false;
  bool locked = false;
  float distance = FLT_MAX;
  float heading = 0.0f;
};

// The nearest ordinary door in front of the player. Load doors (they carry an ExtraTeleport) are skipped: activating
// one would carry the player into another cell in the middle of a recovery.
DoorAhead FindDoorAhead(PlayerCharacter *player) {
  DoorAhead best;
  const NiPoint3 here = player->GetPos();
  const float yawDegrees = Math::ToDegrees(EffectiveYaw(player));
  WorldUtils::ForEachLoadedReference([&](TESObjectREFR *ref, TESObjectCELL *) {
    if (!ref->baseForm || ref->baseForm->eFormType != _FormType::TESObjectDOOR)
      return;
    if (ref->extraDataList.GetExtraData(_ExtraDataType::ExtraTeleport))
      return;
    const NiPoint3 there = ref->GetPos();
    const float distance = Math::GetDistance2D(here, there);
    if (distance > kDoorSearchDistance || distance >= best.distance ||
        std::abs(there.z - here.z) > kDoorMaxHeightDifference)
      return;
    const float heading = Math::GetHeadingBetweenPoints(here.x, here.y, yawDegrees, there.x, there.y);
    if (std::abs(heading) > kDoorSearchCone)
      return;
    best.ref = ref;
    best.distance = distance;
    best.heading = heading;
    best.closed = kGetOpenState(ref) == kOpenStateClosed;
    best.locked = IsLocked(ref);
  });
  return best;
}

// --- Look and movement ----------------------------------------------------------------------------------------

NiPoint3 UpdateSmoothedLookTarget(const PathPoint3 &navigationPoint, float deltaTime) {
  const NiPoint3 desired = {navigationPoint.x, navigationPoint.y, navigationPoint.z};
  if (!g_state.lookTargetInitialized) {
    g_state.smoothedLookTarget = desired;
    g_state.lookTargetInitialized = true;
    return desired;
  }
  const float alpha = 1.0f - std::exp(-kLookTargetResponse * deltaTime);
  g_state.smoothedLookTarget.x += (desired.x - g_state.smoothedLookTarget.x) * alpha;
  g_state.smoothedLookTarget.y += (desired.y - g_state.smoothedLookTarget.y) * alpha;
  g_state.smoothedLookTarget.z += (desired.z - g_state.smoothedLookTarget.z) * alpha;
  return g_state.smoothedLookTarget;
}

void UpdateLook(PlayerCharacter *player, const NiPoint3 &lookTarget, float deltaTime) {
  if (deltaTime <= 0.0f)
    return;
  const float currentYaw = EffectiveYaw(player);
  const float headingError = Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(currentYaw),
                                                           lookTarget.x, lookTarget.y);
  if (std::abs(headingError) < kTurnDeadZone && std::abs(g_state.turnVelocity) < kTurnDeadZone) {
    g_state.turnVelocity = 0.0f;
    return;
  }
  const float desiredVelocity = std::clamp(headingError * kTurnSpeedResponse, -kWalkTurnRate, kWalkTurnRate);
  const float response =
      desiredVelocity * g_state.turnVelocity < 0.0f ? kTurnReversalResponse : kTurnAccelerationResponse;
  const float velocityAlpha = 1.0f - std::exp(-response * deltaTime);
  g_state.turnVelocity += (desiredVelocity - g_state.turnVelocity) * velocityAlpha;

  float turnAmount = g_state.turnVelocity * deltaTime;
  if (turnAmount * headingError > 0.0f && std::abs(turnAmount) > std::abs(headingError)) {
    turnAmount = headingError;
    g_state.turnVelocity = 0.0f;
  }
  SetYaw(player, currentYaw + turnAmount);
}

float UpdatePitch(PlayerCharacter *player, TESObjectREFR *target, bool closeToTarget, float deltaTime) {
  float desiredPitch = 0.0f;
  if (closeToTarget)
    desiredPitch = AimUtils::DesiredPitch(player, AimUtils::TargetPoint(target, AimUtils::Purpose::Look));
  const float alpha = 1.0f - std::exp(-kLookTargetResponse * deltaTime);
  const float pitch = AimUtils::CameraPitch() + (desiredPitch - AimUtils::CameraPitch()) * alpha;
  SetPitch(player, pitch);
  return std::abs(desiredPitch - pitch);
}

void HoldMovementKey(UINT8 key, size_t slot) {
  if (key == kUnboundKey)
    return;
  DIHookControl::GetSingleton().SetKeyHeldState(InputCore::Owner::Walker, key, true);
  g_state.heldMovementKeys[slot] = key;
}

// `headingOffset` turns the walking direction relative to the navigation point (a quarter turn is a sidestep, a half
// turn backs off) without touching the view, so it also works while combat owns the aim.
void UpdateMovement(PlayerCharacter *player, const PathPoint3 &navigationPoint, bool shouldMove,
                    float headingOffset = 0.0f) {
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
  if (!shouldMove)
    return;
  auto *input = OSInputGlobals::GetSingleton();
  if (!input)
    return;

  const float heading = AimLogic::WrapAngle(Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y,
                                                                          Math::ToDegrees(EffectiveYaw(player)),
                                                                          navigationPoint.x, navigationPoint.y) +
                                            headingOffset);
  const float diagonalThreshold = Math::ToRadians(22.5f);
  const float reverseThreshold = Math::ToRadians(112.5f);
  const float sideLimit = Math::ToRadians(157.5f);
  if (std::abs(heading) < Math::ToRadians(67.5f))
    HoldMovementKey(input->keyBinds[ControlCode::Forward], 0);
  if (std::abs(heading) > reverseThreshold)
    HoldMovementKey(input->keyBinds[ControlCode::Backward], 1);
  if (heading > diagonalThreshold && heading < sideLimit)
    HoldMovementKey(input->keyBinds[ControlCode::Right], 2);
  if (heading < -diagonalThreshold && heading > -sideLimit)
    HoldMovementKey(input->keyBinds[ControlCode::Left], 3);
}

// --- Raycast path check and detours (todo 48) -----------------------------------------------------------------
//
// The navmesh does not know every object (the car in Primm): the pathfinder's route can run straight through one.
// On every route search the way ahead is swept with rays (PathProbe::SweepPath). When something blocks it, the
// navigation grid is searched for a walkable way round and the Walker follows its waypoints until it is past or the
// path is free again. When the grid finds none, or she gets stuck anyway, the obstacle is given to the pathfinder to
// route around. On from the start; `NWalkerDetour 0` switches it off, and its other numbers select older forms for
// debugging (a side-step to one point, or rerouting alone).

// The way ahead as the check sweeps it: from the player along the route's nodes from the one steered at (or straight
// at the destination when the pathfinder found nothing), without the last stretch within reach of the destination,
// where the target's own collision would read as an obstacle.
std::vector<PathProbe::Point3> ProbePath(const NiPoint3 &playerPosition, const NiPoint3 &destination) {
  std::vector<PathProbe::Point3> path;
  path.push_back(ToDebugPoint(playerPosition));
  if (g_state.routeNodes.empty()) {
    path.push_back(ToDebugPoint(destination));
  } else {
    for (size_t index = g_state.routeChosen; index < g_state.routeNodes.size(); ++index)
      path.push_back(ToDebugPoint(g_state.routeNodes[index]));
  }
  return PathProbe::TrimEnd(std::move(path), kInteractionDistance);
}

// Gives a blocked spot to the pathfinder as a place to route around, for this walk and the following ones here.
// False when it already had it.
bool AvoidBlockedSpot(PlayerCharacter *player, const PathProbe::BlockMemory::Spot &spot, const char *why) {
  if (!g_blocks.Avoid(spot.point, spot.dirX, spot.dirY, spot.formId))
    return false;

  // The ground the obstacle stands on: the bounds of the object, or a circle just past the blocked spot.
  NiPoint3 centre = {spot.point.x + spot.dirX * kAvoidCircleAhead, spot.point.y + spot.dirY * kAvoidCircleAhead,
                     spot.point.z};
  float radius = kAvoidCircleRadius;
  const char *what = "a circle around the blocked spot";
  if (auto *ref = spot.formId ? LookupReference(spot.formId) : nullptr) {
    NiPoint3 boundCentre;
    float boundRadius = 0.0f;
    if (WorldUtils::ReferenceBound(ref, boundCentre, boundRadius) && boundRadius <= kAvoidMaxBound) {
      centre = {boundCentre.x, boundCentre.y, spot.point.z};
      radius = boundRadius;
      what = "the bounds of the object that blocks";
    }
  }

  // Every navmesh triangle that overlaps it, in the cells it reaches into.
  std::vector<PathPoint3> triangles;
  TESObjectCELL *searched[4] = {};
  for (int corner = 0; corner < 4; ++corner) {
    TESObjectCELL *cell = WorldUtils::CellAt(player, centre.x + ((corner & 1) ? radius : -radius),
                                             centre.y + ((corner & 2) ? radius : -radius));
    if (!cell || std::find(std::begin(searched), std::end(searched), cell) != std::end(searched))
      continue;
    searched[corner] = cell;
    Pathing::TriangleCentresTouching(cell, centre.x, centre.y, radius, centre.z, kAvoidHeightTolerance, triangles);
  }
  std::sort(triangles.begin(), triangles.end(), [&](const PathPoint3 &a, const PathPoint3 &b) {
    return Math::GetDistance2D(a, centre) < Math::GetDistance2D(b, centre);
  });
  if (triangles.size() > kAvoidPerSpot)
    triangles.resize(kAvoidPerSpot);

  size_t added = 0;
  auto add = [&](const PathPoint3 &at, float sphereRadius) {
    for (const Pathing::AvoidSphere &sphere : g_avoid)
      if (Math::GetDistance2D(sphere.centre, at) <= kAvoidSameCentre)
        return;
    g_avoid.push_back({at, sphereRadius, kAvoidCost});
    ++added;
  };
  if (triangles.empty())
    add({centre.x, centre.y, centre.z}, std::max(radius, kAvoidLooseRadius)); // no triangle found: catch what is near
  for (const PathPoint3 &triangle : triangles)
    add(triangle, kAvoidRadius);
  if (g_avoid.size() > kAvoidMaxSpheres)
    g_avoid.erase(g_avoid.begin(), g_avoid.begin() + static_cast<std::ptrdiff_t>(g_avoid.size() - kAvoidMaxSpheres));
  _MESSAGE("Walker reroute: the pathfinder is to avoid %zu navmesh triangle(s) (%zu new) under %s, centre (%.0f, "
           "%.0f, %.0f), radius %.0f: %s",
           triangles.size(), added, what, centre.x, centre.y, centre.z, radius, why);

  g_state.lengthBeforeReroute = g_state.routeLength;
  g_state.reportReroute = true;
  g_state.navigationValid = false; // search again now, with the spot avoided
  return true;
}

void EndDetour(PathProbe::DetourTracker::End why) {
  g_state.detourPlanner.Reset();
  g_state.gridSearch.Reset();
  if (!g_state.detour.Active())
    return;
  _MESSAGE("Walker detour ended: %s", PathProbe::DetourTracker::EndName(why));
  g_state.detour.Finish();
  g_state.detourFromGrid = false;
  g_state.navigationValid = false; // path again from where the detour left the player
  g_debug.view.probe.hasWaypoint = false;
}

// A search on the navigation grid has ended: walk its route, or give the spot to the pathfinder.
void FinishGridSearch(PlayerCharacter *player, float remaining) {
  using State = NavGrid::Search::State;
  NavGrid::Search &search = g_state.gridSearch;
  const State state = search.GetState();
  if (state == State::Working || state == State::Idle)
    return;
  bool usable = state == State::Found;
  if (state == State::Partial) {
    // Not all the way, but well past the near side of what blocks: worth walking, the next check carries on.
    const float fromBlock = Math::GetDistance2D(g_state.searchBlock.point, g_state.gridGoal);
    usable = Math::GetDistance2D(search.Path().back(), g_state.gridGoal) <= fromBlock - kGridPartialGain;
  }
  if (usable) {
    std::vector<PathProbe::Point3> route = NavGrid::MergeStraight(search.Path());
    _MESSAGE("Walker detour %d on the way to %s: the path is blocked by %s; the grid found a way %s, %zu waypoints "
             "over %zu cells (%d cells searched, %d rays, %zu cells known)",
             g_state.detour.Used() + 1, g_state.command.description.c_str(), g_state.detourBlocker.c_str(),
             state == State::Found ? "round" : "part of the way round", route.size(), search.Path().size(),
             search.Expansions(), search.Rays(), g_grid.NodeCount());
    g_state.detour.Begin(std::move(route), 0, remaining);
    g_state.detourFromGrid = true;
    g_blocks.NoteDetour(g_state.searchBlock.point);
    g_state.loggedNoDetour = false;
  } else {
    g_state.detour.PlanFailed();
    _MESSAGE("Walker detour: the path to %s is blocked by %s and the grid found no way round (%d cells searched, %d "
             "rays)",
             g_state.command.description.c_str(), g_state.detourBlocker.c_str(), search.Expansions(), search.Rays());
    AvoidBlockedSpot(player, g_state.searchBlock, "the grid found no way round it");
  }
  search.Reset();
}

// A search for a way round has ended: start walking the detour, or note that there is none.
void FinishDetourSearch(float remaining) {
  PathProbe::DetourPlanner &planner = g_state.detourPlanner;
  const auto state = planner.GetState();
  if (state != PathProbe::DetourPlanner::State::Found && state != PathProbe::DetourPlanner::State::Failed)
    return;
  if (WalkerOverlay::Enabled())
    g_debug.view.probe.candidates = planner.Candidates();
  if (state == PathProbe::DetourPlanner::State::Found) {
    const PathProbe::Candidate &found = planner.Result();
    g_state.detour.Begin(found.point, found.side, remaining);
    g_blocks.NoteDetour(g_state.searchBlock.point);
    g_state.detourRejoin = found.rejoin;
    g_state.loggedNoDetour = false;
    _MESSAGE("Walker detour %d on the way to %s: the path is blocked by %s; going round on the %s through (%.0f, "
             "%.0f, %.0f), %.1f m to the side (%d rays)",
             g_state.detour.Used(), g_state.command.description.c_str(), g_state.detourBlocker.c_str(),
             found.side > 0 ? "left" : "right", found.point.x, found.point.y, found.point.z,
             Units::ToMeters(found.lateral), planner.Rays());
    if (WalkerOverlay::Enabled()) {
      g_debug.view.probe.hasWaypoint = true;
      g_debug.view.probe.waypoint = found.point;
      g_debug.view.probe.rejoin = found.rejoin;
    }
  } else {
    g_state.detour.PlanFailed();
    if (!g_state.loggedNoDetour || WalkerOverlay::Logs(WalkerOverlay::LogLevel::Routes)) {
      g_state.loggedNoDetour = true;
      int firstLeg = 0;
      int offNavmesh = 0;
      int onward = 0;
      for (const PathProbe::Candidate &candidate : planner.Candidates()) {
        firstLeg += candidate.status == PathProbe::CandidateStatus::FirstLegBlocked;
        offNavmesh += candidate.status == PathProbe::CandidateStatus::NotWalkable;
        onward += candidate.status == PathProbe::CandidateStatus::OnwardBlocked;
      }
      _MESSAGE("Walker detour: the path to %s is blocked by %s and no way round was found (%d points: %d with no free "
               "line to them, %d off the navmesh, %d with no free line onward; %d rays)",
               g_state.command.description.c_str(), g_state.detourBlocker.c_str(),
               static_cast<int>(planner.Candidates().size()), firstLeg, offNavmesh, onward, planner.Rays());
    }
    AvoidBlockedSpot(PlayerCharacter::GetSingleton(), g_state.searchBlock, "there is no way round beside it");
  }
  planner.Reset();
}

// One frame of the check while the Walker walks its route (not while standing within reach or in a recovery step).
void UpdateDetour(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &playerPosition,
                  const NiPoint3 &destination, float remaining, float deltaTime) {
  using PathProbe::Blocker;
  using PathProbe::DetourTracker;
  DetourTracker &tracker = g_state.detour;
  PathProbe::DetourPlanner &planner = g_state.detourPlanner;
  const bool searched = g_state.routeRefreshed; // rays are cast when the route was searched: every 0.25 s
  g_state.routeRefreshed = false;
  tracker.Update(deltaTime, remaining);
  if (!WalkerProbe::DetourEnabled()) {
    EndDetour(DetourTracker::End::Abandoned);
    return;
  }

  const PathProbe::Point3 here = ToDebugPoint(playerPosition);
  // The side rays stand the player's own collision radius apart. Without it there is no check: a made-up width would
  // decide which gaps she fits through.
  PathProbe::Settings settings;
  float bodyHeight = 0.0f;
  if (!WorldUtils::PlayerCollisionShape(player, settings.halfWidth, bodyHeight)) {
    if (!g_state.loggedNoShape) {
      g_state.loggedNoShape = true;
      _WARNING("Walker path check: the player's collision shape cannot be read; no check on this walk");
    }
    EndDetour(DetourTracker::End::Abandoned);
    return;
  }
  const bool debug = WalkerOverlay::Enabled();
  WalkerProbe::EngineWorld world(player, target);
  const double started = WalkerProbe::NowMicroseconds();
  auto account = [&] { g_state.probeCost.Add(world.Rays(), WalkerProbe::NowMicroseconds() - started); };

  if (tracker.Active()) {
    DetourTracker::End why = DetourTracker::End::Abandoned;
    if (tracker.Expired(here.x, here.y, why)) {
      EndDetour(why);
      return;
    }
    if (!searched)
      return;
    // Still a free line to the waypoint? And is the path itself free again from here, with room to spare?
    const PathProbe::SweepResult leg =
        PathProbe::Sweep(world, here, tracker.Waypoint(), kProbeNoLimit, settings, settings.halfWidth);
    if (!leg.Clear()) {
      account();
      EndDetour(DetourTracker::End::LegBlocked);
      return;
    }
    // A grid route has a waypoint at every turn of its cells; when the one after next can be walked to directly,
    // the next one is not needed.
    const std::vector<PathProbe::Point3> ahead2 = tracker.Remaining();
    if (ahead2.size() >= 2 &&
        PathProbe::Sweep(world, here, ahead2[1], kProbeNoLimit, settings, settings.halfWidth).Clear())
      tracker.SkipAhead(1);
    PathProbe::Trace trace;
    const PathProbe::SweepResult ahead =
        PathProbe::SweepPath(world, ProbePath(playerPosition, destination), kProbeLookAhead, settings,
                             settings.halfWidth * kProbeClearWidthFactor, debug ? &trace : nullptr);
    account();
    if (debug)
      g_debug.view.probe.rays = std::move(trace.rays);
    if (ahead.blocker != Blocker::Obstacle && ahead.blocker != Blocker::Rise)
      EndDetour(DetourTracker::End::PathClear);
    else
      g_state.lastBlock = {ahead.blockPoint, ahead.dirX, ahead.dirY, ahead.hit.formId};
    return;
  }

  if (g_state.gridSearch.GetState() == NavGrid::Search::State::Working) {
    g_state.gridSearch.Step(world, settings, kGridRayBudget);
    account();
    FinishGridSearch(player, remaining);
    return;
  }
  if (planner.GetState() == PathProbe::DetourPlanner::State::Working) {
    planner.Step(world, here, settings, kProbeRayBudget);
    account();
    FinishDetourSearch(remaining);
    return;
  }
  if (!searched)
    return;

  std::vector<PathProbe::Point3> path = ProbePath(playerPosition, destination);
  PathProbe::Trace trace;
  const PathProbe::SweepResult ahead =
      PathProbe::SweepPath(world, path, kProbeLookAhead, settings, 0.0f, debug ? &trace : nullptr);
  // A drop on the pathfinder's own route is its business (it may mean to jump down); a door is opened by the stuck
  // recovery, not walked around.
  const bool blocked = ahead.blocker == Blocker::Obstacle || ahead.blocker == Blocker::Rise;
  if (debug) {
    g_debug.view.probe.rays = std::move(trace.rays);
    g_debug.view.probe.hasBlock = blocked;
    g_debug.view.probe.block = ahead.blockPoint;
    if (!blocked)
      g_debug.view.probe.candidates.clear();
  }
  g_state.hasLastBlock = blocked;
  if (blocked)
    g_state.lastBlock = {ahead.blockPoint, ahead.dirX, ahead.dirY, ahead.hit.formId};
  if (!blocked || PathProbe::PathLength(path) - ahead.clearDistance < kProbeMinOnward) {
    account();
    return;
  }
  // A side-step first; when one was already taken for this spot it did not get her past, and the pathfinder has to
  // find another way. When the pathfinder already knows and still routes through, there is no other way.
  const auto action = g_blocks.Decide(ahead.blockPoint, WalkerProbe::RerouteOnly(), ahead.hit.formId);
  if (action == PathProbe::BlockMemory::Action::Reroute) {
    account();
    AvoidBlockedSpot(player, g_state.lastBlock,
                     WalkerProbe::RerouteOnly() ? "the route is blocked there" : "the side-step did not get past it");
    return;
  }
  if (action == PathProbe::BlockMemory::Action::Nothing || !tracker.MayPlan()) {
    account();
    return;
  }
  g_state.searchBlock = g_state.lastBlock;
  char buffer[128];
  if (ahead.blocker == Blocker::Rise)
    std::snprintf(buffer, sizeof(buffer), "a rise of %.0f units %.1f m ahead: ", ahead.step,
                  Units::ToMeters(ahead.clearDistance));
  else
    std::snprintf(buffer, sizeof(buffer), "%s %.1f m ahead: ", PathProbe::BlockerName(ahead.blocker),
                  Units::ToMeters(ahead.clearDistance));
  g_state.detourBlocker = buffer + WalkerProbe::DescribeHit(ahead.hit);
  if (WalkerProbe::GridEnabled()) {
    // The grid looks for a walkable way from here to the route beyond the obstacle, on or off the navmesh.
    g_state.gridGoal =
        PathProbe::PointAlong(path, std::min(ahead.clearDistance + kGridRejoin, PathProbe::PathLength(path)));
    g_state.gridSearch.Start(g_grid, here, g_state.gridGoal);
    g_state.gridSearch.Step(world, settings, kGridRayBudget);
    account();
    FinishGridSearch(player, remaining);
    return;
  }
  planner.Start(std::move(path), ahead, tracker.PreferredSide());
  planner.Step(world, here, settings, kProbeRayBudget);
  account();
  FinishDetourSearch(remaining);
}

// --- Stuck recovery -------------------------------------------------------------------------------------------

// What a frame of recovery asks of the movement code.
struct MovementPlan {
  bool move = true;
  float headingOffset = 0.0f;
  TESObjectREFR *door = nullptr; // face this door instead of the path
};

bool IsJumpBound() { return InputUtils::ResolveControl(static_cast<int>(ControlCode::Jump)) != InputUtils::kNoControl; }

void PulseJump() {
  const uint32_t macro = InputUtils::ResolveControl(static_cast<int>(ControlCode::Jump));
  if (macro != InputUtils::kNoControl)
    g_jumpPulses.Pulse(macro, kJumpPulseSeconds);
}

void EndRecovery() {
  g_state.recovery = {};
  g_state.stuck.StartGrace(0.0f); // forget the history so the verdict needs a fresh window
  g_state.oscillation.Reset();
  g_state.slide.Reset();
  g_state.navigationValid = false; // the next frame paths again, from wherever the step left the player
}

// A random spot on the navmesh near the player that the pathfinder can actually reach from here. Objects that are not
// in the navmesh (rubble, clutter) are what trapped the player, so a spot found by walking the navmesh gives a new
// angle on them. False when no candidate qualified.
bool PickDetour(PlayerCharacter *player, int tier, NiPoint3 &out) {
  static std::mt19937 random{std::random_device{}()};
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  auto *cell = GetNavigationCell(player);
  if (!cell)
    return false;
  const NiPoint3 here = player->GetPos();
  for (int candidate = 0; candidate < kDetourCandidates; ++candidate) {
    float dx = 0.0f;
    float dy = 0.0f;
    WalkerLogic::DetourOffset(tier, unit(random), dx, dy);
    const NiPoint3 wanted = {here.x + dx, here.y + dy, here.z};
    NiPoint4 closest = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    Pathing::GetClosestNavMeshTriangle(cell, wanted, false, 0.0f, closest);
    if (closest.x == FLT_MAX)
      continue;
    const NiPoint3 snapped = {closest.x, closest.y, closest.z};
    if (Math::GetDistance2D(snapped, wanted) > kDetourSnapTolerance ||
        Math::GetDistance2D(snapped, here) < kDetourMinDistance)
      continue;
    PathPoint3 firstNode;
    if (!BuildPathPoint(player, snapped, firstNode))
      continue;
    out = snapped;
    return true;
  }
  return false;
}

// Chooses and starts a recovery step. False when the walk was failed instead (the command is gone then).
bool BeginRecovery(PlayerCharacter *player, float remaining, const char *why) {
  if (g_state.detour.Active() && g_state.detourFromGrid) {
    // Stuck on a route the grid called walkable: whatever the rays missed, that step is blocked from now on.
    const NiPoint3 here = player->GetPos();
    const PathProbe::Point3 &waypoint = g_state.detour.Waypoint();
    g_grid.Block({here.x, here.y, here.z}, waypoint.x - here.x, waypoint.y - here.y);
  }
  if (WalkerProbe::DetourEnabled()) {
    // Stuck at something: the pathfinder should stop sending her there. At a spot the path check saw blocked (or was
    // side-stepping) at once; anywhere else from the third time in a row, with the place right ahead of her.
    const NiPoint3 here = player->GetPos();
    if (g_state.hasLastBlock && Math::GetDistance2D(here, g_state.lastBlock.point) <= kStuckAtBlockDistance) {
      AvoidBlockedSpot(player, g_state.lastBlock, "she got stuck at it");
    } else if (g_state.planner.Attempts() >= kStuckAttemptsBeforeAvoid && IsValidPoint(g_state.navigationPoint)) {
      const float dx = g_state.navigationPoint.x - here.x;
      const float dy = g_state.navigationPoint.y - here.y;
      const float length = std::sqrt(dx * dx + dy * dy);
      if (length > 1.0f)
        AvoidBlockedSpot(player, {{here.x, here.y, here.z}, dx / length, dy / length}, "she keeps getting stuck here");
    }
  }
  EndDetour(PathProbe::DetourTracker::End::Abandoned); // the recovery steps take over from here
  const DoorAhead door = FindDoorAhead(player);
  if (door.ref && door.closed && door.locked) {
    g_state.lockedDoor = DoorLabel(door.ref);
    if (door.distance <= kDoorBlockingDistance && std::abs(door.heading) <= kDoorBlockingCone) {
      FinishFailed(player, WalkerLogic::FailureReason::DoorLocked, g_state.lockedDoor);
      return false;
    }
  }
  const bool doorOpenable = door.ref && door.closed && !door.locked;
  auto choice = g_state.planner.Next(IsJumpBound(), doorOpenable, remaining);
  if (!choice) {
    if (g_state.lockedDoor.empty())
      FinishFailed(player, WalkerLogic::FailureReason::Stuck);
    else
      FinishFailed(player, WalkerLogic::FailureReason::DoorLocked, g_state.lockedDoor);
    return false;
  }

  NiPoint3 detour = {0.0f, 0.0f, 0.0f};
  if (choice->step == WalkerLogic::RecoveryStep::Wander && !PickDetour(player, choice->tier, detour)) {
    _WARNING("Walker found no reachable spot to wander to; backing off instead");
    choice->step = WalkerLogic::RecoveryStep::BackOff;
  }
  g_state.recovery = {};
  g_state.recovery.active = true;
  g_state.recovery.hasDetour = choice->step == WalkerLogic::RecoveryStep::Wander;
  g_state.recovery.detour = detour;
  g_state.detourStuck.Reset();
  g_state.navigationValid = false; // a Wander step steers at its own spot from the next search on
  g_state.recovery.step = choice->step;
  g_state.recovery.mirrored = choice->mirrored;
  g_state.recovery.doorFormId = choice->step == WalkerLogic::RecoveryStep::Door ? door.ref->GetFormID() : 0;
  g_state.oscillation.Reset();
  g_state.slide.Reset();
  g_state.stuck.StartGrace(0.0f);
  _MESSAGE("Walker recovery attempt %d (%s): step %d%s", g_state.planner.Attempts(), why,
           static_cast<int>(choice->step), choice->mirrored ? " mirrored" : "");
  return true;
}

MovementPlan StepRecovery(PlayerCharacter *player, float deltaTime) {
  using WalkerLogic::RecoveryStep;
  Recovery &recovery = g_state.recovery;
  recovery.elapsed += deltaTime;
  MovementPlan plan;
  switch (recovery.step) {
  case RecoveryStep::Door: {
    plan.move = false;
    auto *door = LookupReference(recovery.doorFormId);
    if (!door) {
      EndRecovery();
      return plan;
    }
    plan.door = door;
    if (!recovery.doorActivated) {
      const float heading = Math::GetHeadingBetweenPoints(
          player->pos.x, player->pos.y, Math::ToDegrees(EffectiveYaw(player)), door->pos.x, door->pos.y);
      // Combat owns the view while it is engaged, so do not wait for a turn that will not happen.
      if (std::abs(heading) <= kDoorFaceTolerance || recovery.elapsed >= kDoorFaceTimeout || g_lookSuppressed) {
        if (kGetOpenState(door) == kOpenStateClosed && !IsLocked(door)) {
          CALL_MEMBER_FN(door, Activate)(player, 0, 0, 1);
          _MESSAGE("Walker opened %s during recovery", DoorLabel(door).c_str());
        }
        recovery.doorActivated = true;
        recovery.doorActivatedAt = recovery.elapsed;
      }
    } else if (recovery.elapsed - recovery.doorActivatedAt >= kDoorOpenWait) {
      EndRecovery();
      return plan;
    }
    break;
  }
  case RecoveryStep::Jump:
    if (recovery.jumps == 0 || (recovery.jumps == 1 && recovery.elapsed >= kSecondJumpAt)) {
      PulseJump();
      ++recovery.jumps;
    }
    break;
  case RecoveryStep::Sidestep:
    plan.headingOffset = recovery.mirrored ? -kQuarterTurn : kQuarterTurn;
    break;
  case RecoveryStep::BackOff:
    plan.headingOffset = kHalfTurn;
    break;
  case RecoveryStep::Wander: {
    const NiPoint3 here = player->GetPos();
    if (Math::GetDistance2D(here, recovery.detour) <= kDetourArrivedDistance ||
        g_state.detourStuck.Update(deltaTime, here.x, here.y, true))
      EndRecovery();
    break;
  }
  }
  if (recovery.active && recovery.elapsed >= WalkerLogic::StepDuration(recovery.step))
    EndRecovery();
  return plan;
}

// --- Completion -----------------------------------------------------------------------------------------------

void UpdateLineOfSight(PlayerCharacter *player, TESObjectREFR *target, float deltaTime) {
  g_state.lineOfSightTimer -= deltaTime;
  if (g_state.lineOfSightTimer > 0.0f)
    return;
  g_state.lineOfSightTimer = kLineOfSightInterval;
  const bool clear = WorldUtils::HasLineOfSight(player, target, AimUtils::TargetPoint(target, AimUtils::Purpose::Look),
                                                {kInteractionRayStart, kInteractionRayEnd});
  if (clear != g_state.lineOfSight)
    _DMESSAGE("Walker line of sight to %s changed: %s", g_state.command.description.c_str(),
              clear ? "clear" : "blocked");
  g_state.lineOfSight = clear;
}

// How close the player must get before the command counts as arrived. Only an interaction with an actor reaches
// further: the actor walks about, so it is engaged from kActorInteractionDistance and stays engaged up to the release
// distance.
float InteractionReach(TESObjectREFR *target) {
  if (g_state.command.intent != Intent::Interact || !target->IsActor())
    return kInteractionDistance;
  return g_state.engaged ? kActorInteractionRelease : kActorInteractionDistance;
}

void TryComplete(PlayerCharacter *player, TESObjectREFR *target, float distance, float reach, bool aligned,
                 float aimError, float deltaTime) {
  if (distance >= reach) {
    g_state.engaged = false;
    g_state.interaction.Reset();
    g_state.blockedTime = 0.0f;
    g_state.lineOfSightTimer = 0.0f;
    return;
  }
  const bool chasingActor = g_state.command.intent == Intent::Interact && target->IsActor();
  g_state.engaged = chasingActor;
  ReleaseMovementKeys();
  UpdateLineOfSight(player, target, deltaTime);
  if (g_state.command.intent == Intent::Move) {
    // Within 100 units in 3D can still be another floor or behind a wall: reached means a clear ray or the same level.
    const bool sameLevel = std::abs(target->GetPos().z - player->GetPos().z) <= kSameLevelHeight;
    if (g_state.lineOfSight || sameLevel) {
      FinishCompleted(player, "Reached " + g_state.command.description + ".");
      return;
    }
    g_state.blockedTime += deltaTime;
    if (g_state.blockedTime >= kMoveBlockedGiveUpDelay)
      FinishFailed(player, WalkerLogic::FailureReason::WrongFloor);
    return;
  }

  // Activation does not need the crosshair on the target: one second after the aim is done with a clear ray, or after
  // the give-up delay whatever held it up (see WalkerLogic::InteractionGate).
  const auto verdict = g_state.interaction.Update(deltaTime, aligned, aimError, g_state.lineOfSight, chasingActor);
  if (verdict == WalkerLogic::InteractionGate::Verdict::Wait)
    return;
  if (verdict == WalkerLogic::InteractionGate::Verdict::GiveUp)
    _WARNING("Walker could not line up with %s (%s); interacting anyway", g_state.command.description.c_str(),
             !g_state.lineOfSight ? "line of sight blocked" : "view still turning");
  else if (!aligned && !chasingActor)
    _MESSAGE("Walker aim settled off %s (aim error %.3f rad); interacting", g_state.command.description.c_str(),
             aimError);
  if (!IsInteractable(target)) {
    FinishFailed(player, WalkerLogic::FailureReason::NotInteractable);
    return;
  }

  if (!CALL_MEMBER_FN(target, Activate)(player, 0, 0, 1)) {
    FinishFailed(player, WalkerLogic::FailureReason::ActivationRejected);
    return;
  }
  _MESSAGE("Walker interacted with: %s", g_state.command.description.c_str());
  FinishCompleted(player, "Reached and interacted with " + g_state.command.description + ".");
}

// The progress reminder. Combat's approach is constant and has its own end conditions, so it never reminds.
void UpdateReminder(float deltaTime, float walked, float remaining) {
  if (g_state.command.owner == Owner::Combat)
    return;
  const auto snapshot = g_state.reminder.Update(deltaTime, walked, g_state.command.owner == Owner::Exploration);
  if (!snapshot)
    return;
  Event event = MakeEvent(EventType::Progress);
  event.walkedDistance = snapshot->walkedDistance;
  event.walkSeconds = snapshot->walkSeconds;
  event.remainingDistance = remaining;
  g_events.push_back(std::move(event));
}

} // namespace

bool Start(uint32_t targetFormId, Intent intent, Owner owner, std::string description) {
  if (!targetFormId || owner == Owner::None || !LookupReference(targetFormId))
    return false;
  if (g_state.hasCommand && g_state.command.targetFormId == targetFormId && g_state.command.intent == intent &&
      g_state.command.owner == owner) {
    g_state.command.description = std::move(description);
    return true;
  }

  ClearCommand(PlayerCharacter::GetSingleton());
  ResetDebugCapture();
  g_state.command = {targetFormId, intent, owner, std::move(description)};
  g_state.hasCommand = true;
  g_state.planner.Reset(RetryBudget());
  // CombatHandler closes in on melee targets constantly; announcing each approach would only be noise.
  if (owner != Owner::Combat) {
    const char *activity = owner == Owner::Exploration  ? "Exploring toward "
                           : owner == Owner::Quest      ? "Following "
                           : intent == Intent::Interact ? "Moving to interact with "
                                                        : "Moving to ";
    Event event = MakeEvent(EventType::Started);
    event.message = std::string(activity) + g_state.command.description + ".";
    g_events.push_back(std::move(event));
  }
  _MESSAGE("Walker started target %08X with intent %d and owner %d", targetFormId, static_cast<int>(intent),
           static_cast<int>(owner));
  return true;
}

bool CanInteract(uint32_t targetFormId) { return IsInteractable(LookupReference(targetFormId)); }

void Process() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!g_state.hasCommand)
    return;
  if (!player || StartMenu::Get()) {
    Pause(PauseReason::Menu);
    return;
  }
  if ((player->pcControlFlags & (PlayerCharacter::kControlFlag_Movement | PlayerCharacter::kControlFlag_Look)) != 0) {
    Pause(PauseReason::Controls);
    return;
  }
  if (!EnsureWaypointMarker(player)) {
    FinishFailed(player, WalkerLogic::FailureReason::NoWaypoint);
    return;
  }

  auto *target = LookupReference(g_state.command.targetFormId);
  if (!target) {
    FinishFailed(player, WalkerLogic::FailureReason::TargetGone);
    return;
  }
  const bool debug = WalkerOverlay::Enabled();
  if (debug != g_debug.wasEnabled) {
    ResetDebugCapture(); // switched on mid-walk: start from this route, not one from before
    g_debug.wasEnabled = debug;
  }
  const float deltaTime = TimeUtils::FrameSeconds();
  g_state.pauseNotice.Update(deltaTime, false);
  g_jumpPulses.Advance(deltaTime);

  auto *currentCell = player->GetParentCell();
  if (target != g_state.trackedTarget || currentCell != g_state.trackedCell) {
    const bool changedCell = g_state.trackedCell && g_state.trackedCell != currentCell;
    // Walking into the next exterior cell on the same walk is not a new route: keep the recovery budget and the
    // progress stall, or a walk stuck near a cell border starts over every time it crosses it (the corner loop of the
    // 2026-11-07 playtest). A load door or any other jump is a new route.
    const bool sameWalk = changedCell && target == g_state.trackedTarget && g_state.positionInitialized &&
                          Math::GetDistance2D(player->GetPos(), g_state.lastPlayerPosition) <=
                              WalkerLogic::StuckDetector::kTeleportDistance;
    const WalkerLogic::RecoveryPlanner planner = g_state.planner;
    const WalkerLogic::ProgressTracker progress = g_state.progress;
    const PathProbe::DetourTracker detour = g_state.detour;
    ReleaseMovementKeys();
    ResetRouteState();
    g_state.trackedTarget = target;
    g_state.trackedCell = currentCell;
    if (sameWalk) {
      g_state.planner = planner;
      g_state.progress = progress;
      g_state.detour = detour; // the waypoint is in world coordinates and the detour budget belongs to the walk
    } else if (changedCell) {
      g_state.stuck.StartGrace(kCellChangeGrace); // positions jump and the world is still streaming in
      ResetDebugCapture();                        // the old route and trail are in another place
    }
  }

  // Blocked spots belong to the interior or worldspace they were found in.
  const uint32_t space = !currentCell                ? 0
                         : currentCell->IsInterior() ? currentCell->GetFormID()
                         : currentCell->worldSpace   ? currentCell->worldSpace->GetFormID()
                                                     : 0;
  if (space != g_avoidSpace) {
    g_avoidSpace = space;
    ForgetSpace();
  }

  const NiPoint3 playerPosition = player->GetPos();
  float walked = 0.0f;
  float frameDx = 0.0f;
  float frameDy = 0.0f;
  if (g_state.positionInitialized) {
    walked = Math::GetDistance2D(playerPosition, g_state.lastPlayerPosition);
    frameDx = playerPosition.x - g_state.lastPlayerPosition.x;
    frameDy = playerPosition.y - g_state.lastPlayerPosition.y;
    if (walked > WalkerLogic::StuckDetector::kTeleportDistance)
      walked = frameDx = frameDy = 0.0f;
  }
  g_state.lastPlayerPosition = playerPosition;
  g_state.positionInitialized = true;
  if (debug) {
    g_debug.trail.Add(ToDebugPoint(playerPosition));
    g_debug.view.player = ToDebugPoint(playerPosition);
    g_debug.view.target = ToDebugPoint(target->GetPos());
    g_debug.view.hasTarget = true;
  }

  const float distance = Distance3D(playerPosition, target->GetPos());
  const float reach = InteractionReach(target);
  const bool shouldMove = distance >= reach;
  PathPoint3 navigationPoint = GetNavigationPoint(player, target, deltaTime);
  // The raycast path check runs while she walks her route; standing within reach or a recovery step ends a detour.
  if (shouldMove && !g_state.recovery.active)
    UpdateDetour(player, target, playerPosition, target->GetPos(), distance, deltaTime);
  else
    EndDetour(PathProbe::DetourTracker::End::Abandoned);
  g_state.routeRefreshed = false;
  if (g_state.detour.Active()) {
    const PathProbe::Point3 &waypoint = g_state.detour.Waypoint();
    navigationPoint = {waypoint.x, waypoint.y, waypoint.z};
  }

  // Progress is the straight distance to the target, except once the pathfinder has been sent round a blocked spot:
  // that way can lead away from the target for a long time, so the length of its route counts instead.
  const bool alongRoute = !g_avoid.empty() && g_state.routeLength >= 0.0f;
  const float progressDistance = alongRoute ? g_state.routeLength : distance;
  if (alongRoute != g_state.progressAlongRoute) {
    g_state.progressAlongRoute = alongRoute; // the two measures cannot be compared: start over
    g_state.progress.Reset();
  }

  MovementPlan plan;
  plan.move = shouldMove;
  if (!shouldMove) {
    g_state.stuck.Update(deltaTime, playerPosition.x, playerPosition.y, false);
    g_state.oscillation.Reset();
    g_state.progress.Update(deltaTime, progressDistance, false);
    g_state.slide.Reset();
    g_state.recovery = {};
  } else if (g_state.progress.Update(deltaTime, progressDistance, true)) {
    // Counts through recovery steps too: a walk that only ever recovers is not getting anywhere either.
    FinishFailed(player, WalkerLogic::FailureReason::NoProgress);
    return;
  } else if (g_state.recovery.active) {
    plan = StepRecovery(player, deltaTime);
  } else if (g_state.gridSearch.GetState() == NavGrid::Search::State::Working) {
    // She stands while the grid looks for a way round (a few frames to a few seconds): walking on would be into the
    // obstacle, and the search starts from where she stood. Standing is not being stuck.
    plan.move = false;
    g_state.stuck.Update(deltaTime, playerPosition.x, playerPosition.y, false);
    g_state.oscillation.Reset();
    g_state.slide.Reset();
  } else {
    g_state.planner.Update(distance);
    const bool stuck = g_state.stuck.Update(deltaTime, playerPosition.x, playerPosition.y, true);
    bool sliding = false;
    if (g_state.stuck.InGrace() || !IsValidPoint(navigationPoint))
      g_state.slide.Reset();
    else
      sliding = g_state.slide.Update(deltaTime, frameDx, frameDy, navigationPoint.x - playerPosition.x,
                                     navigationPoint.y - playerPosition.y, true);
    const bool oscillating = g_state.oscillation.Update(deltaTime, distance);
    if (stuck || sliding || oscillating) {
      const char *why = stuck     ? "no player movement"
                        : sliding ? "running into an obstacle"
                                  : "target distance oscillation";
      if (!BeginRecovery(player, distance, why))
        return;
      plan = StepRecovery(player, 0.0f);
      navigationPoint = GetNavigationPoint(player, target, 0.0f); // a Wander step steers at its own spot
    }
  }

  float pitchError = 0.0f;
  if (!g_lookSuppressed) {
    NiPoint3 lookTarget = target->GetPos();
    TESObjectREFR *lookObject = target;
    bool closeToTarget = distance < kCloseLookDistance;
    if (plan.door) {
      lookTarget = plan.door->GetPos();
      lookObject = plan.door;
      closeToTarget = true;
    } else if (shouldMove) {
      lookTarget = UpdateSmoothedLookTarget(navigationPoint, deltaTime);
    }
    UpdateLook(player, lookTarget, deltaTime);
    pitchError = UpdatePitch(player, lookObject, closeToTarget, deltaTime);
  }
  const float headingError = Math::GetHeadingBetweenPoints(
      player->pos.x, player->pos.y, Math::ToDegrees(EffectiveYaw(player)), target->pos.x, target->pos.y);
  const bool aligned = std::abs(headingError) <= kLookAlignmentTolerance && pitchError <= kLookAlignmentTolerance;
  UpdateMovement(player, navigationPoint, plan.move, plan.headingOffset);
  UpdateReminder(deltaTime, walked, distance);
  TryComplete(player, target, distance, reach, aligned, std::abs(headingError) + pitchError, deltaTime);
  if (g_state.hasCommand)
    PublishDebug(true);
}

void Pause(PauseReason reason) {
  auto *player = PlayerCharacter::GetSingleton();
  ReleaseMovementKeys();
  g_jumpPulses.ReleaseAll();
  Player::SetAutoMove(player, false);
  if (!g_state.hasCommand)
    return;
  if (!g_state.pauseNotice.Update(RealFrameDelta(), true))
    return;
  const char *cause = reason == PauseReason::PipBoy     ? "the Pip-Boy is being used"
                      : reason == PauseReason::Controls ? "your controls are disabled (a scripted scene or cutscene)"
                                                        : "a menu is open";
  Event event = MakeEvent(EventType::Paused);
  event.detail = cause;
  g_events.push_back(std::move(event));
}

void Stop() {
  ClearCommand(PlayerCharacter::GetSingleton());
  g_events.clear();
}

void ResetDebug() {
  ResetDebugCapture();
  WalkerOverlay::Clear();
  // Called on a load, a new game and the main menu: what was blocked or sampled may not be in this game's world.
  ForgetSpace();
  g_avoidSpace = 0;
}

bool IsActive() { return g_state.hasCommand; }

Owner GetOwner() { return g_state.hasCommand ? g_state.command.owner : Owner::None; }

uint32_t GetTargetFormId() { return g_state.hasCommand ? g_state.command.targetFormId : 0; }

std::string GetDescription() { return g_state.hasCommand ? g_state.command.description : std::string(); }

void SetLookSuppressed(bool suppressed) { g_lookSuppressed = suppressed; }

std::vector<Event> TakeEvents() {
  std::vector<Event> events;
  events.swap(g_events);
  return events;
}

} // namespace Walker
