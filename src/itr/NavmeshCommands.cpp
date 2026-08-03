// navmesh probes exposed as script commands
// GetPathLength reuses the standalone PathingRequest/Solution flow from PathingCommands
// IsPointOnNavmesh resolves a triangle via a stack PathingLocation
// GetCoverPointsInRadius resolves each cell NavMeshInfo to its NavMesh and reads the cover triangle array

#include "itr/NavmeshCommands.h"
#include "itr/PathingShared.h"
#include "itr/internal/EngineFunctions.h"
#include "itr/internal/GameGlobals.h"
#include "itr/internal/RayCast.h"
#include "nvse/CommandTable.h"
#include "nvse/GameAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/ParamInfos.h"
#include "nvse/PluginAPI.h"

#include <cmath>
#include <cstdint>
#include <memory>

extern const _ExtractArgs ExtractArgs;
extern NVSEArrayVarInterface *g_arrayInterface;

namespace {
DWORD s_mainThreadId = 0;

bool IsMainThread() { return !s_mainThreadId || GetCurrentThreadId() == s_mainThreadId; }

bool IsActorRef(TESObjectREFR *ref) {
  if (!ref || !ref->baseForm)
    return false;
  return ref->baseForm->eFormType == (kVtbl_Creature) ||
         ref->baseForm->eFormType == (kVtbl_TESNPC); // Was kFormType_NPC idk if this is the correct correction
}

float DistanceSq(const PathPoint3 &a, const PathPoint3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

float PathDistance(Actor *actor, TESObjectREFR *target) {
  if (!actor || !target || !target->parentCell)
    return -1.0f;

  ScopedPathingRequest request;
  ScopedPathingSolution solution;

  PathPoint3 destination = {target->pos.x, target->pos.y, target->pos.z};
  CdeclCall<void>(0x9DBC90, actor, request.Get(), &destination, target->parentCell, target->parentCell->worldSpace,
                  0.0f, static_cast<void *>(nullptr)); // PathManager::BuildPath

  if (!CdeclCall<bool>(0x6D0900, request.Get(), solution.Get())) // PathManager::Solve
    return -1.0f;

  auto *solutionData = solution.Get();
  if (solutionData->incompletePath)
    return -1.0f;

  const INT32 nodeCount = ThisCall<INT32>(0x8B6800, solution.Get()); // PathingSolution::GetNodeCount

  float distance = 0.0f;
  PathPoint3 prev = {};
  bool havePrev = false;
  for (INT32 i = 0; i < nodeCount; ++i) {
    auto *node = ThisCall<PathingNodeLayout *>(0x6E7970, solution.Get(), i); // PathingSolution::GetNode
    if (!node)
      break;
    const PathPoint3 &pos = node->pathingLocation.location;
    if (havePrev) {
      const float dx = pos.x - prev.x;
      const float dy = pos.y - prev.y;
      const float dz = pos.z - prev.z;
      distance += sqrtf(dx * dx + dy * dy + dz * dz);
    }
    prev = pos;
    havePrev = true;
  }

  return havePrev ? distance : -1.0f;
}

bool PointOnNavmesh(const PathPoint3 &point, TESObjectCELL *cell, TESWorldSpace *worldSpace) {
  if (!cell)
    return false;

  alignas(4) UINT8 loc[sizeof(PathingLocationLayout)] = {};
  ThisCall<void>(0x6DCEE0, loc, &point, cell, worldSpace); // PathingLocation::PathingLocation

  // stored navMeshInfo/navMeshes are borrowed engine pointers, no owned refs to release
  const bool resolved = ThisCall<bool>(0x6DD6F0, loc, 0); // PathingLocation::ResolveTriangle
  const UINT16 triangle = reinterpret_cast<PathingLocationLayout *>(loc)->triangle;
  return resolved && triangle != 0xFFFF;
}
constexpr INT32 kMaxCoverPoints = 128;

// nearest-N by distance to the query point, so the result never depends on cell scan order
struct CoverPool {
  CoverPoint items[kMaxCoverPoints];
  INT32 count = 0;
};

void InsertCover(CoverPool &pool, const CoverPoint &item) {
  if (pool.count == kMaxCoverPoints && item.distSq >= pool.items[kMaxCoverPoints - 1].distSq)
    return;
  INT32 pos = (pool.count < kMaxCoverPoints) ? pool.count : (kMaxCoverPoints - 1);
  while (pos > 0 && pool.items[pos - 1].distSq > item.distSq) {
    pool.items[pos] = pool.items[pos - 1];
    --pos;
  }
  pool.items[pos] = item;
  if (pool.count < kMaxCoverPoints)
    ++pool.count;
}

void GatherCellCover(TESObjectCELL *cell, const PathPoint3 &query, float radiusSq, CoverPool &pool) {
  if (!cell)
    return;

  // array of NavMeshInfo*, one per navmesh that could serve this cell
  void *container = CdeclCall<void *>(0x6D6F40, cell); // Pathing::GetPotentialNavMeshInfoForLocation
  if (!container)
    return;

  auto *infoArr = reinterpret_cast<BSSimpleArrayLayout<void *> *>(container);
  if (!infoArr->data)
    return;

  for (INT32 n = 0; n < infoArr->size; ++n) {
    void *info = infoArr->data[n];
    if (!info)
      continue;

    // fills an owning NiPointer<NavMesh> from NavMeshInfo+0x54, false when the info is invalid or the mesh is not
    // loaded
    ScopedNavMeshPtr navMesh;
    if (!ThisCall<bool>(0x69AD00, info, navMesh.Slot()))
      continue;

    UINT8 *mesh = static_cast<UINT8 *>(navMesh.Get());
    auto *coverArr = reinterpret_cast<BSSimpleArrayLayout<UINT16> *>(mesh + 0x78); // cover triangle indices
    auto *triArr = reinterpret_cast<BSSimpleArrayLayout<UINT8> *>(mesh + 0x38);    // triangles, stride 0x10
    if (!coverArr->data || !triArr->data)
      continue;

    const INT32 coverCount = coverArr->size;
    const INT32 triCount = triArr->size;
    for (INT32 c = 0; c < coverCount; ++c) {
      const UINT16 triIndex = coverArr->data[c];
      if (triIndex >= triCount)
        continue;

      void *tri = triArr->data + triIndex * 0x10;
      if (ThisCall<bool>(0x691140, tri, 0x20)) // NavMeshTriangle::IsFlagSet, 0x20 = disabled
        continue;

      for (INT32 slot = 0; slot < 2; ++slot) {
        UINT16 bucket = 0;
        bool leftOpen = false, rightOpen = false;
        ThisCall<void>(0x691040, tri, slot, &bucket, &leftOpen, &rightOpen); // NavMeshTriangle::GetEdgeCoverData

        if (!(bucket >= 2 || leftOpen || rightOpen))
          continue;

        EdgeEndpoints edge = {};
        ThisCall<void>(0x68F040, mesh, &edge, triIndex, slot); // NavMesh::GetEdgeEndpoints
        if (!edge.p0 || !edge.p1)
          continue;

        CoverPoint point;
        point.pos.x = (edge.p0->x + edge.p1->x) * 0.5f;
        point.pos.y = (edge.p0->y + edge.p1->y) * 0.5f;
        point.pos.z = (edge.p0->z + edge.p1->z) * 0.5f;
        point.distSq = DistanceSq(point.pos, query);
        if (point.distSq > radiusSq)
          continue;

        point.flags = (bucket & 0xF) | (leftOpen ? 0x10 : 0) | (rightOpen ? 0x20 : 0) | (slot << 6);
        InsertCover(pool, point);
      }
    }
  }
}

float AxisGap(float v, float lo, float hi) {
  if (v < lo)
    return lo - v;
  if (v > hi)
    return v - hi;
  return 0.0f;
}

void GatherCoverAround(const PathPoint3 &query, float radius, CoverPool &pool) {
  PlayerCharacter *player = *g_thePlayerPtr;
  if (!player || !player->parentCell)
    return;

  const float radiusSq = radius * radius;
  TESObjectCELL *anchorCell = player->parentCell;
  if (anchorCell->IsInterior()) {
    GatherCellCover(anchorCell, query, radiusSq, pool);
    return;
  }

  TESWorldSpace *world = anchorCell->worldSpace;
  if (!world || !world->pCellMap)
    return;

  const INT32 centreX = static_cast<INT32>(floorf(query.x / 4096.0f));
  const INT32 centreY = static_cast<INT32>(floorf(query.y / 4096.0f));
  INT32 cellRadius = static_cast<INT32>(ceilf(radius / 4096.0f));
  if (cellRadius > 2)
    cellRadius = 2;

  for (INT32 dx = -cellRadius; dx <= cellRadius; ++dx) {
    for (INT32 dy = -cellRadius; dy <= cellRadius; ++dy) {
      const INT32 cellX = centreX + dx;
      const INT32 cellY = centreY + dy;
      const float minX = cellX * 4096.0f;
      const float minY = cellY * 4096.0f;
      const float gapX = AxisGap(query.x, minX, minX + 4096.0f);
      const float gapY = AxisGap(query.y, minY, minY + 4096.0f);
      if (gapX * gapX + gapY * gapY > radiusSq)
        continue;

      // mask before shifting, negative cell coords make the raw shift formally UB
      const INT32 key = (((INT32)cellX & 0xFFFF) << 16) | ((INT32)cellY & 0xFFFF);
      TESObjectCELL *cell = nullptr;
      world->pCellMap->GetAt(key, cell);
      GatherCellCover(cell, query, radiusSq, pool);
    }
  }
}

constexpr UINT8 kLayerLineOfSight = 37;    // LAYER_LINEOFSIGHT, engine vision filter
constexpr float kThreatEyeHeight = 96.0f;  // eye height approximation
constexpr float kCoverTorsoHeight = 64.0f; // crouched torso height

// true when the threat's view of the standing position is blocked, so the point is usable cover
bool ThreatBlockedFromPoint(const PathPoint3 &threat, const PathPoint3 &cover) {
  RayCastData ray = {};
  ray.pos0[0] = threat.x * kHavokScale;
  ray.pos0[1] = threat.y * kHavokScale;
  ray.pos0[2] = (threat.z + kThreatEyeHeight) * kHavokScale;
  ray.pos1[0] = cover.x * kHavokScale;
  ray.pos1[1] = cover.y * kHavokScale;
  ray.pos1[2] = (cover.z + kCoverTorsoHeight) * kHavokScale;
  ray.hitFraction = 1.0f;
  ray.unk44[0] = 0xFFFFFFFF;
  ray.unk44[6] = 0xFFFFFFFF;
  ray.layerType = kLayerLineOfSight;

  // future: sample low/mid/high like the engine GetLineOfSight three-ray probe
  if (!Engine::TESPickObject(&ray, true))
    return false; // no cast performed, cannot confirm blockage, treat as exposed
  return ray.hitFraction < 1.0f;
}

constexpr INT32 kMaxBestCover = 32;

constexpr float kMaxWorldCoord = 1.0e7f; // far past any worldspace extent, keeps the cell grid maths exact in float
constexpr float kMaxCoverRadius = 8192.0f;

bool ValidCoord(float v) {
  return v > -kMaxWorldCoord && v < kMaxWorldCoord; // nan and inf fail both comparisons
}

bool ValidRadius(float r) {
  return r > 0.0f && r <= kMaxCoverRadius; // nan and inf fail
}
} // namespace
bool Cmd_GetPathLength_Execute(COMMAND_ARGS) {
  *result = -1.0;

  TESObjectREFR *target = nullptr;
  TESObjectREFR *source = nullptr;
  if (!ExtractArgs(EXTRACT_ARGS, &target, &source))
    return true;

  if (!IsMainThread())
    return true;

  TESObjectREFR *actorRef = source ? source : thisObj;
  if (!target || !IsActorRef(actorRef))
    return true;

  *result = PathDistance(reinterpret_cast<Actor *>(actorRef), target);
  return true;
}

bool Cmd_IsPointOnNavmesh_Execute(COMMAND_ARGS) {
  *result = 0;

  float x = 0, y = 0, z = 0;
  TESObjectREFR *anchorRef = nullptr;
  if (!ExtractArgs(EXTRACT_ARGS, &x, &y, &z, &anchorRef))
    return true;

  if (!IsMainThread())
    return true;

  TESObjectCELL *cell = nullptr;
  if (anchorRef)
    cell = anchorRef->parentCell;
  else if (PlayerCharacter *player = *g_thePlayerPtr)
    cell = player->parentCell;

  if (!cell)
    return true;

  PathPoint3 point = {x, y, z};
  if (PointOnNavmesh(point, cell, cell->worldSpace))
    *result = 1;

  return true;
}

bool Cmd_GetCoverPointsInRadius_Execute(COMMAND_ARGS) {
  *result = 0;
  if (!g_arrayInterface)
    return true;

  float x = 0, y = 0, z = 0, radius = 0;
  if (!ExtractArgs(EXTRACT_ARGS, &x, &y, &z, &radius))
    return true;

  if (!IsMainThread())
    return true;

  if (!ValidRadius(radius) || !ValidCoord(x) || !ValidCoord(y) || !ValidCoord(z))
    return true;

  const PathPoint3 query = {x, y, z};

  CoverPool pool;
  GatherCoverAround(query, radius, pool);

  NVSEArrayVarInterface::Array *arr = g_arrayInterface->CreateArray(nullptr, 0, scriptObj);
  if (!arr)
    return true;
  for (INT32 i = 0; i < pool.count; ++i) {
    const CoverPoint &point = pool.items[i];
    NVSEArrayVarInterface::Array *sub = g_arrayInterface->CreateArray(nullptr, 0, scriptObj);
    if (!sub)
      continue;
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(point.pos.x));
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(point.pos.y));
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(point.pos.z));
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(static_cast<double>(point.flags)));
    g_arrayInterface->AppendElement(arr, NVSEArrayVarInterface::Element(sub));
  }
  g_arrayInterface->AssignCommandResult(arr, result);

  return true;
}

bool Cmd_GetBestCoverFromThreat_Execute(COMMAND_ARGS) {
  *result = 0;
  if (!g_arrayInterface)
    return true;

  float threatX = 0, threatY = 0, threatZ = 0;
  float searchX = 0, searchY = 0, searchZ = 0, radius = 0;
  INT32 maxResults = 8;
  if (!ExtractArgs(EXTRACT_ARGS, &threatX, &threatY, &threatZ, &searchX, &searchY, &searchZ, &radius, &maxResults))
    return true;

  if (!IsMainThread())
    return true;

  if (!ValidRadius(radius))
    return true;
  if (!ValidCoord(threatX) || !ValidCoord(threatY) || !ValidCoord(threatZ))
    return true;
  if (!ValidCoord(searchX) || !ValidCoord(searchY) || !ValidCoord(searchZ))
    return true;

  if (maxResults < 1)
    maxResults = 1;
  if (maxResults > static_cast<INT32>(kMaxBestCover))
    maxResults = kMaxBestCover;

  const PathPoint3 threat = {threatX, threatY, threatZ};
  const PathPoint3 search = {searchX, searchY, searchZ};

  CoverPool pool;
  GatherCoverAround(search, radius, pool);

  NVSEArrayVarInterface::Array *arr = g_arrayInterface->CreateArray(nullptr, 0, scriptObj);
  if (!arr)
    return true;

  // pool is already nearest-first, so the raycasts stop once maxResults have passed
  INT32 emitted = 0;
  for (INT32 i = 0; i < pool.count && emitted < static_cast<INT32>(maxResults); ++i) {
    const CoverPoint &point = pool.items[i];
    if (!ThreatBlockedFromPoint(threat, point.pos))
      continue;
    NVSEArrayVarInterface::Array *sub = g_arrayInterface->CreateArray(nullptr, 0, scriptObj);
    if (!sub)
      continue;
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(point.pos.x));
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(point.pos.y));
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(point.pos.z));
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(static_cast<double>(point.flags)));
    g_arrayInterface->AppendElement(sub, NVSEArrayVarInterface::Element(static_cast<double>(sqrtf(point.distSq))));
    g_arrayInterface->AppendElement(arr, NVSEArrayVarInterface::Element(sub));
    ++emitted;
  }
  g_arrayInterface->AssignCommandResult(arr, result);

  return true;
}
