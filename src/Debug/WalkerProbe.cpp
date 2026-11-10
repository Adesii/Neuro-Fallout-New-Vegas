#include "Debug/WalkerProbe.hpp"

#include "Context/Distance.hpp"
#include "Debug/WalkerOverlay.hpp"
#include "GameData.h"
#include "GameObjects.h"
#include "GameProcess.h"
#include "Gamebryo/NiPoint3.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "decoding.h"
#include "havok.h"
#include "itr/PathingCommands.h"
#include "nvse/GameAPI.h"
#include "nvse/GameForms.h"
#include "nvse/ParamInfos.h"
#include "utils/AimUtils.hpp"
#include "utils/WorldUtils.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace WalkerProbe {
namespace {

using PathProbe::Point3;

bool g_detourEnabled = false;
bool g_requireNavmesh = true;
uint8_t g_rayLayer = WorldUtils::Layer::kLineOfSight;

// A point counts as on the navmesh when a triangle lies under it within this height (GetPointNavMesh's zLimit).
constexpr float kNavmeshHeightTolerance = 150.0f;
constexpr float kExteriorCellSize = 4096.0f;

NiPoint3 ToNi(const Point3 &p) { return {p.x, p.y, p.z}; }

// The cell a point lies in: the interior, or the exterior cell of the player's worldspace at those coordinates
// (looked up like src/itr/NavmeshCommands.cpp does for cover points), the player's own cell when that is not loaded.
TESObjectCELL *CellAt(PlayerCharacter *player, float x, float y) {
  auto *tes = TES::GetSingleton();
  if (tes && tes->currentInterior)
    return tes->currentInterior;
  TESObjectCELL *own = player ? player->GetParentCell() : nullptr;
  TESWorldSpace *world = own ? own->worldSpace : nullptr;
  if (!world || !world->pCellMap)
    return own;
  const INT32 cellX = static_cast<INT32>(std::floor(x / kExteriorCellSize));
  const INT32 cellY = static_cast<INT32>(std::floor(y / kExteriorCellSize));
  const INT32 key = ((cellX & 0xFFFF) << 16) | (cellY & 0xFFFF);
  TESObjectCELL *cell = nullptr;
  world->pCellMap->GetAt(key, cell);
  return cell ? cell : own;
}

std::string DescribeReference(TESObjectREFR *ref, uint8_t layer) {
  char buffer[192];
  if (!ref) {
    std::snprintf(buffer, sizeof(buffer), "terrain or unnamed geometry (%s)", WorldUtils::LayerName(layer));
    return buffer;
  }
  const char *name = ref->GetFullName();
  std::snprintf(buffer, sizeof(buffer), "%08X \"%s\" base type %u (%s)", static_cast<unsigned>(ref->GetFormID()),
                name ? name : "", ref->baseForm ? static_cast<unsigned>(ref->baseForm->eFormType) : 0u,
                WorldUtils::LayerName(layer));
  return buffer;
}

// --- NWalkerProbe -----------------------------------------------------------------------------------------------

struct Burst {
  double microseconds = 0.0;
  int rays = 0;
  int hits = 0;
  int budgetSpent = 0; // rays that came back with the payload's byte at 0xAC set
  double Each() const { return rays > 0 ? microseconds / rays : 0.0; }
};

Burst RunBurst(int count, uint8_t layer, uint16_t group, bool findReference,
               const std::function<void(int, NiPoint3 &, NiPoint3 &)> &ray) {
  Burst burst;
  burst.rays = count;
  const double start = NowMicroseconds();
  for (int index = 0; index < count; ++index) {
    NiPoint3 from;
    NiPoint3 to;
    ray(index, from, to);
    const WorldUtils::RayDetail detail = WorldUtils::CastRay(from, to, layer, group, findReference);
    burst.hits += detail.hit;
    burst.budgetSpent += detail.budgetSpent;
  }
  burst.microseconds = NowMicroseconds() - start;
  return burst;
}

void LogRay(const char *what, uint8_t layer, const NiPoint3 &from, const NiPoint3 &to, uint16_t group) {
  const WorldUtils::RayDetail detail = WorldUtils::CastRay(from, to, layer, group);
  const float length = WorldUtils::Distance3D(from, to);
  if (!detail.performed) {
    _MESSAGE("Walker probe:   as %-14s %-5s no cast could be made", WorldUtils::LayerName(layer), what);
    return;
  }
  if (!detail.hit) {
    _MESSAGE("Walker probe:   as %-14s %-5s clear for %.0f units%s", WorldUtils::LayerName(layer), what, length,
             detail.budgetSpent ? " (budget byte set)" : "");
    return;
  }
  _MESSAGE("Walker probe:   as %-14s %-5s hit after %.0f units (%.2f m): %s; normal (%.2f, %.2f, %.2f)%s",
           WorldUtils::LayerName(layer), what, length * detail.fraction, Units::ToMeters(length * detail.fraction),
           DescribeReference(detail.ref, detail.layer).c_str(), detail.normal.x, detail.normal.y, detail.normal.z,
           detail.budgetSpent ? " (budget byte set)" : "");
}

// The player's character controller shape, raw. The offsets are named by JIP-LN-NVSE only (`internal/havok.h`,
// bhkCharacterController: scale 0x55C, actorHeight 0x564, shapeDefaultRadius 0x5F0, shapeHeight 0x5F4, shapeRadius
// 0x5F8); the vendored JG definition has unnamed words there. The radius and height the Walker uses come from
// WorldUtils::PlayerCollisionShape; this line shows the neighbouring fields too.
void LogControllerShape(PlayerCharacter *player) {
  auto *process = player->baseProcess;
  auto *controller =
      process && process->processLevel <= 1 ? static_cast<MiddleHighProcess *>(process)->charCtrl : nullptr;
  if (!controller) {
    _MESSAGE("Walker probe: no character controller to read the collision shape from.");
    return;
  }
  auto read = [controller](size_t offset) {
    float value = 0.0f;
    std::memcpy(&value, reinterpret_cast<const uint8_t *>(controller) + offset, sizeof(value));
    return value;
  };
  const float radius = read(0x5F8);
  const float height = read(0x5F4);
  float usedRadius = 0.0f;
  float usedHeight = 0.0f;
  const bool usable = WorldUtils::PlayerCollisionShape(player, usedRadius, usedHeight);
  _MESSAGE("Walker probe: character controller (JIP-LN offsets, Havok units): shape radius %.4f, shape height %.4f, "
           "default radius %.4f, actor height %.4f, scale %.3f. In game units: radius %.1f, height %.1f (%s).",
           radius, height, read(0x5F0), read(0x564), read(0x55C), usedRadius, usedHeight,
           usable ? "used as the half width of the side rays" : "NOT usable: the Walker's path check stays off");
}

} // namespace

bool DetourEnabled() { return g_detourEnabled; }

uint8_t RayLayer() { return g_rayLayer; }

double NowMicroseconds() {
  static LARGE_INTEGER frequency = [] {
    LARGE_INTEGER value;
    QueryPerformanceFrequency(&value);
    return value;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<double>(now.QuadPart) * 1.0e6 / static_cast<double>(frequency.QuadPart);
}

EngineWorld::EngineWorld(PlayerCharacter *player, TESObjectREFR *target)
    : m_player(player), m_target(target), m_group(WorldUtils::PlayerCollisionGroup(player)) {}

PathProbe::RayHit EngineWorld::Cast(const Point3 &from, const Point3 &to) {
  ++m_rays;
  const WorldUtils::RayDetail detail = WorldUtils::CastRay(ToNi(from), ToNi(to), g_rayLayer, m_group);
  PathProbe::RayHit hit;
  if (!detail.hit)
    return hit;
  TESObjectREFR *ref = detail.ref;
  if (ref && ref == m_player)
    return hit; // the player's own body despite the group: not an obstacle
  hit.hit = true;
  hit.fraction = detail.fraction;
  hit.layer = detail.layer;
  if (!ref)
    return hit;
  hit.formId = ref->GetFormID();
  if (ref == m_target)
    hit.kind = PathProbe::HitKind::Target;
  else if (ref->IsActor())
    hit.kind = PathProbe::HitKind::Actor;
  else if (ref->baseForm && ref->baseForm->eFormType == _FormType::TESObjectDOOR)
    hit.kind = PathProbe::HitKind::Door;
  return hit;
}

bool EngineWorld::Walkable(const Point3 &point) {
  if (!g_requireNavmesh)
    return true;
  TESObjectCELL *cell = CellAt(m_player, point.x, point.y);
  if (!cell)
    return false;
  if (Pathing::PointOnNavmesh({point.x, point.y, point.z}, cell, cell->worldSpace))
    return true;
  NiPoint4 found = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
  return Pathing::GetPointNavMesh(cell, ToNi(point), false, kNavmeshHeightTolerance, found);
}

std::string DescribeHit(const PathProbe::RayHit &hit) {
  return DescribeReference(hit.formId ? WorldUtils::LookupReference(hit.formId) : nullptr, hit.layer);
}

// `NWalkerDetour` toggles the detours, `NWalkerDetour 1` / `0` set them, `NWalkerDetour 2` switches them on without
// requiring the waypoint to be on the navmesh. An optional second number is the collision layer the rays are cast as
// (37 line of sight, the default; 30 character controller; see WorldUtils::Layer). Returns the new state.
DEFINE_NEURO_COMMAND_PLUGIN(WalkerDetour, "Walker raycast detours: [0 off/1 on/2 on, off-navmesh allowed] [ray layer]",
                            false, kParams_TwoOptionalInts);

bool Cmd_NWalkerDetour_Execute(COMMAND_ARGS) {
  int mode = -1;
  int layer = -1;
  if (!ExtractArgsEx(EXTRACT_ARGS_EX, &mode, &layer))
    return true;
  if (layer > 0 && layer < WorldUtils::Layer::kCount)
    g_rayLayer = static_cast<uint8_t>(layer);
  if (mode < 0) {
    g_detourEnabled = !g_detourEnabled;
  } else {
    g_detourEnabled = mode != 0;
    if (mode != 0)
      g_requireNavmesh = mode != 2;
  }
  *result = g_detourEnabled ? 1.0 : 0.0;
  _MESSAGE("Walker detours %s (rays cast as %s, waypoints %s).", g_detourEnabled ? "on" : "off",
           WorldUtils::LayerName(g_rayLayer), g_requireNavmesh ? "must be on the navmesh" : "may be off the navmesh");
  Console_Print("Walker detours %s; rays cast as layer %d (%s); waypoints %s.", g_detourEnabled ? "on" : "off",
                static_cast<int>(g_rayLayer), WorldUtils::LayerName(g_rayLayer),
                g_requireNavmesh ? "must be on the navmesh" : "may be off the navmesh");
  if (g_detourEnabled && !WalkerOverlay::Enabled())
    Console_Print("NWalkerDebug 1 draws the probe rays (grey clear, red blocked) and the detour (violet).");
  return true;
}

// `NWalkerProbe [rays] [layer]`: from where the player stands, looking where the player looks. Writes to
// neuro-fnv.log: the character controller's shape, what a knee, a chest and a floor ray hit when cast as each of
// several collision layers, what a burst of `rays` (default 2000) rays costs, and what the Walker's own sweep makes of
// the 600 units ahead. With the overlay on (NWalkerDebug 1) the sweep's rays are drawn. `layer` limits the layer
// comparison and sets the layer of the bursts; without it the bursts use the detours' current layer.
DEFINE_NEURO_COMMAND_PLUGIN(WalkerProbe, "Measures and logs raycasts ahead of the player: [rays 100-20000] [layer]",
                            false, kParams_TwoOptionalInts);

bool Cmd_NWalkerProbe_Execute(COMMAND_ARGS) {
  int count = 2000;
  int layerArgument = -1;
  if (!ExtractArgsEx(EXTRACT_ARGS_EX, &count, &layerArgument))
    return true;
  *result = 0.0;
  auto *player = PlayerCharacter::GetSingleton();
  if (!player || !player->GetParentCell()) {
    Console_Print("Walker probe: no player in a loaded cell.");
    return true;
  }
  count = std::clamp(count, 100, 20000);
  const bool oneLayer = layerArgument > 0 && layerArgument < WorldUtils::Layer::kCount;
  const uint8_t burstLayer = oneLayer ? static_cast<uint8_t>(layerArgument) : g_rayLayer;
  const uint16_t group = WorldUtils::PlayerCollisionGroup(player);
  const NiPoint3 feet = player->GetPos();
  const float yaw = AimUtils::EffectiveYaw(player);
  const float forwardX = std::sin(yaw);
  const float forwardY = std::cos(yaw);
  PathProbe::Settings settings;
  float bodyHeight = 0.0f;
  const bool hasShape = WorldUtils::PlayerCollisionShape(player, settings.halfWidth, bodyHeight);

  _MESSAGE("Walker probe: player at (%.0f, %.0f, %.0f) in cell %08X, facing (%.2f, %.2f), collision group %u.", feet.x,
           feet.y, feet.z, static_cast<unsigned>(player->GetParentCell()->GetFormID()), forwardX, forwardY,
           static_cast<unsigned>(group));
  LogControllerShape(player);

  // What the same three rays hit when cast as different layers.
  static constexpr uint8_t kCompared[] = {WorldUtils::Layer::kLineOfSight, WorldUtils::Layer::kCharController,
                                          WorldUtils::Layer::kPathPick,    WorldUtils::Layer::kProjectile,
                                          WorldUtils::Layer::kCameraPick,  WorldUtils::Layer::kBiped};
  constexpr float kAhead = 600.0f;
  _MESSAGE("Walker probe: rays %.0f units ahead at knee (%.0f) and chest (%.0f) height, and one down to the floor "
           "100 units ahead:",
           kAhead, settings.kneeHeight, settings.chestHeight);
  auto compare = [&](uint8_t layer) {
    const NiPoint3 knee = {feet.x, feet.y, feet.z + settings.kneeHeight};
    const NiPoint3 chest = {feet.x, feet.y, feet.z + settings.chestHeight};
    LogRay("knee", layer, knee, {knee.x + forwardX * kAhead, knee.y + forwardY * kAhead, knee.z}, group);
    LogRay("chest", layer, chest, {chest.x + forwardX * kAhead, chest.y + forwardY * kAhead, chest.z}, group);
    const NiPoint3 above = {feet.x + forwardX * 100.0f, feet.y + forwardY * 100.0f, feet.z + settings.floorAbove};
    LogRay("floor", layer, above, {above.x, above.y, feet.z - settings.floorBelow}, group);
  };
  if (oneLayer)
    compare(burstLayer);
  else
    for (uint8_t layer : kCompared)
      compare(layer);

  // What a ray costs. Rays fan out around the player so they cross different parts of the scene.
  constexpr float kTwoPi = 6.2831853f;
  auto fan = [&](float height, float length) {
    return [=](int index, NiPoint3 &from, NiPoint3 &to) {
      const float angle = kTwoPi * static_cast<float>(index) / static_cast<float>(count);
      from = {feet.x, feet.y, feet.z + height};
      to = {from.x + std::sin(angle) * length, from.y + std::cos(angle) * length, from.z};
    };
  };
  auto floorRays = [=](int index, NiPoint3 &from, NiPoint3 &to) {
    // A spiral out to 10 meters, like the floor samples of a grid around the player.
    const float t = static_cast<float>(index) / static_cast<float>(count);
    const float angle = kTwoPi * 12.0f * t;
    const float radius = 700.0f * t;
    from = {feet.x + std::sin(angle) * radius, feet.y + std::cos(angle) * radius, feet.z + 60.0f};
    to = {from.x, from.y, feet.z - 120.0f};
  };
  const Burst shortRays = RunBurst(count, burstLayer, group, false, fan(settings.chestHeight, 60.0f));
  const Burst mediumRays = RunBurst(count, burstLayer, group, false, fan(settings.chestHeight, 400.0f));
  const Burst longRays = RunBurst(count, burstLayer, group, false, fan(settings.chestHeight, 3500.0f));
  const Burst downRays = RunBurst(count, burstLayer, group, false, floorRays);
  const Burst withReference = RunBurst(count, burstLayer, group, true, fan(settings.chestHeight, 400.0f));
  auto logBurst = [&](const char *what, const Burst &burst) {
    _MESSAGE("Walker probe:   %-34s %5d rays, %5d hits, %8.2f ms in all, %7.2f microseconds each, budget byte set "
             "on %d",
             what, burst.rays, burst.hits, burst.microseconds / 1000.0, burst.Each(), burst.budgetSpent);
  };
  _MESSAGE("Walker probe: cost of rays cast as %s:", WorldUtils::LayerName(burstLayer));
  logBurst("60 units, chest height, all round", shortRays);
  logBurst("400 units, chest height, all round", mediumRays);
  logBurst("3500 units, chest height, all round", longRays);
  logBurst("180 units down, within 10 m", downRays);
  logBurst("400 units, with reference lookup", withReference);

  // What the Walker's sweep makes of the way ahead.
  EngineWorld world(player, nullptr);
  const Point3 start = {feet.x, feet.y, feet.z};
  const Point3 end = {feet.x + forwardX * kAhead, feet.y + forwardY * kAhead, feet.z};
  PathProbe::Trace trace;
  double clock = NowMicroseconds();
  const PathProbe::SweepResult centre = PathProbe::Sweep(world, start, end, kAhead, settings, 0.0f, &trace);
  const double centreTime = NowMicroseconds() - clock;
  clock = NowMicroseconds();
  // Without the collision radius there is no width to sweep at; the second sweep is then left out.
  const PathProbe::SweepResult wide =
      hasShape ? PathProbe::Sweep(world, start, end, kAhead, settings, settings.halfWidth, &trace)
               : PathProbe::SweepResult{};
  const double wideTime = NowMicroseconds() - clock;
  auto logSweep = [&](const char *what, const PathProbe::SweepResult &sweep, double microseconds) {
    if (sweep.Clear())
      _MESSAGE("Walker probe: sweep %s: clear for %.0f units, floor ends %.0f above the start; %d rays, %.0f "
               "microseconds",
               what, sweep.clearDistance, sweep.end.z - feet.z, sweep.rays, microseconds);
    else if (sweep.blocker == PathProbe::Blocker::Rise || sweep.blocker == PathProbe::Blocker::Drop)
      _MESSAGE("Walker probe: sweep %s: blocked after %.0f units (%.2f m) by %s (the floor changes by %.0f units "
               "within %.0f; limits +%.0f / -%.0f): %s; %d rays, %.0f microseconds",
               what, sweep.clearDistance, Units::ToMeters(sweep.clearDistance), PathProbe::BlockerName(sweep.blocker),
               sweep.step, settings.groundStep, settings.maxRise, settings.maxDrop, DescribeHit(sweep.hit).c_str(),
               sweep.rays, microseconds);
    else
      _MESSAGE("Walker probe: sweep %s: blocked after %.0f units (%.2f m) by %s: %s; %d rays, %.0f microseconds", what,
               sweep.clearDistance, Units::ToMeters(sweep.clearDistance), PathProbe::BlockerName(sweep.blocker),
               DescribeHit(sweep.hit).c_str(), sweep.rays, microseconds);
  };
  logSweep("on the centre line", centre, centreTime);
  if (hasShape)
    logSweep("at the player's width", wide, wideTime);
  const bool onNavmesh = world.Walkable(start);
  _MESSAGE("Walker probe: the player's position is %s.",
           !g_requireNavmesh ? "not checked against the navmesh (the requirement is off)"
           : onNavmesh       ? "on the navmesh"
                             : "NOT on the navmesh");

  if (WalkerOverlay::Enabled()) {
    RouteDebug::View view;
    view.player = start;
    view.probe.rays = std::move(trace.rays);
    if (!centre.Clear()) {
      view.probe.hasBlock = true;
      view.probe.block = centre.blockPoint;
    }
    WalkerOverlay::Publish(view);
  }
  Console_Print("Walker probe (%s): %.1f microseconds a ray at 400 units, %.1f at 60, %.1f at 3500, %.1f down.",
                WorldUtils::LayerName(burstLayer), mediumRays.Each(), shortRays.Each(), longRays.Each(),
                downRays.Each());
  if (centre.Clear())
    Console_Print("Ahead: clear for %.0f units. Details in neuro-fnv.log.", centre.clearDistance);
  else
    Console_Print("Ahead: blocked after %.1f m by %s. Details in neuro-fnv.log.", Units::ToMeters(centre.clearDistance),
                  PathProbe::BlockerName(centre.blocker));
  *result = mediumRays.Each();
  return true;
}

void RegisterCommands(NVSEInterface *nvse) {
  REG_CMD(nvse, WalkerDetour);
  REG_CMD(nvse, WalkerProbe);
}

} // namespace WalkerProbe
