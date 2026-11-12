// temporary path probes exposed as script commands
// builds a standalone PathingRequest/Solution instead of reading an actor's live mover path

#include "itr/PathingCommands.h"
#include "JG/internal/decoding.h"
#include "Movement/PathProbe.hpp"
#include "PathingShared.h"
#include "nvse/CommandTable.h"
#include "nvse/GameAPI.h"
#include "nvse/GameForms.h"
#include "nvse/GameObjects.h"
#include "nvse/ParamInfos.h"
#include "nvse/PluginAPI.h"

#include <algorithm>
#include <cmath>
#include <vector>

extern const _ExtractArgs ExtractArgs;
extern NVSEArrayVarInterface *g_arrayInterface;

namespace Pathing {

constexpr UINT32 kMaxPathNodes = 1024;

DWORD s_mainThreadId = 0;

bool IsActorRef(TESObjectREFR *ref) {
  if (!ref || !ref->baseForm)
    return false;
  return ref->baseForm->eFormType == kVtbl_Creature ||
         ref->baseForm->eFormType == kVtbl_TESNPC; // Was kFormType_NPC idk if this is the correct correction
}

bool IsMainThread() { return !s_mainThreadId || GetCurrentThreadId() == s_mainThreadId; }

float Distance(const PathPoint3 &a, const PathPoint3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return sqrtf(dx * dx + dy * dy + dz * dz);
}

float ComputeDistance(const std::vector<PathPoint3> &nodes) {
  float distance = 0.0f;
  for (size_t i = 1; i < nodes.size(); ++i)
    distance += Distance(nodes[i - 1], nodes[i]);
  return distance;
}

// The engine's avoid nodes (added for neuro-FNV, not from itr-nvse). Read from FalloutNV.exe 1.4.0.525 itself, and
// matching JohnnyGuitarNVSE's declarations (PathingAvoidNode.hpp, PathingRequest.hpp) and JIP-LN-NVSE (`avoidNodeArray
// // 94`, kVtbl_PathingAvoidNodeArray):
// - The seventh argument of 0x9DBC90 goes to PathingRequest::SetAvoidNodeArray (0x6D61E0), a smart-pointer assignment
//   at request+0x94 that takes a reference; the request's destructor (0x6E2620) drops it, which frees the array.
// - The engine makes one with its allocator (0xAA13E0, 0x18 bytes) and the constructor 0x6E3850, and appends nodes
//   with 0x905450, which copies 0x24 bytes (the function at 0x9E71B0 does all three for the actors around a mover).
// - A node is a point, a second point (cylinders only), radius (0x18), cost (0x1C) and type (0x20); type 0 is a
//   sphere. The search (0x6A74C8 and its siblings) multiplies a triangle's cost by 1 + the sum over the nodes of
//   cost * (1 - d^2 / r^2), d being the distance from the node to the triangle's centre (0x6DC990, 0x6DC780). The
//   engine's own nodes have cost 2.0.
struct PathingAvoidNodeLayout {
  PathPoint3 point1;
  PathPoint3 point2;
  float radius;
  float cost;
  UINT32 type;
};
static_assert(sizeof(PathingAvoidNodeLayout) == 0x24);

static void *MakeAvoidNodeArray(const std::vector<AvoidSphere> &spheres) {
  void *memory = CdeclCall<void *>(0xAA13E0, 0x18u);
  if (!memory)
    return nullptr;
  void *array = ThisCall<void *>(0x6E3850, memory); // PathingAvoidNodeArray::PathingAvoidNodeArray
  for (const AvoidSphere &sphere : spheres) {
    PathingAvoidNodeLayout node = {};
    node.point1 = sphere.centre;
    node.radius = sphere.radius;
    node.cost = sphere.cost;
    node.type = 0;                            // SPHERE
    ThisCall<UINT32>(0x905450, array, &node); // BSSimpleArray<PathingAvoidNode>::Append
  }
  return array;
}

bool BuildPath(Actor *actor, TESObjectREFR *target, PathResult &out, const std::vector<AvoidSphere> *avoid) {
  out = PathResult();

  if (!actor || !target || !target->parentCell)
    return false;

  ScopedPathingRequest request;
  ScopedPathingSolution solution;

  // Owned by the request from the call below on (it starts with no references; the request takes the only one).
  void *avoidNodes = avoid && !avoid->empty() ? MakeAvoidNodeArray(*avoid) : nullptr;
  PathPoint3 destination = {target->pos.x, target->pos.y, target->pos.z};
  CdeclCall<void>(0x9DBC90, actor, request.Get(), &destination, target->parentCell, target->parentCell->worldSpace,
                  0.0f, avoidNodes);

  if (!CdeclCall<bool>(0x6D0900, request.Get(), solution.Get()))
    return false;

  auto *solutionData = solution.Get();
  if (solutionData->incompletePath)
    return false;

  const UINT32 nodeCount = ThisCall<UINT32>(0x8B6800, solution.Get());
  const UINT32 safeCount = (std::min)(nodeCount, kMaxPathNodes);
  out.nodes.reserve(safeCount);

  for (UINT32 i = 0; i < safeCount; ++i) {
    auto *node = ThisCall<PathingNodeLayout *>(0x6E7970, solution.Get(), i);
    if (!node)
      break;
    out.nodes.push_back(node->pathingLocation.location);
  }

  if (out.nodes.empty())
    return false;

  out.distance = ComputeDistance(out.nodes);
  out.complete = true;
  return true;
}

bool GetPath(TESObjectREFR *actorRef, TESObjectREFR *target, PathResult &out) {
  out = PathResult();

  if (!IsMainThread() || !actorRef || !target || !IsActorRef(actorRef))
    return false;

  BuildPath(static_cast<Actor *>(actorRef), target, out);
  return out.complete;
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

NVSEArrayVarInterface::Array *CreateArray(Script *scriptObj) {
  if (!g_arrayInterface)
    return nullptr;
  return g_arrayInterface->CreateArray(nullptr, 0, scriptObj);
}

void AssignArray(NVSEArrayVarInterface::Array *arr, double *result) {
  if (g_arrayInterface && arr)
    g_arrayInterface->AssignCommandResult(arr, result);
}

void AppendPointComponents(NVSEArrayVarInterface::Array *arr, const PathPoint3 &point) {
  g_arrayInterface->AppendElement(arr, NVSEArrayVarInterface::Element(point.x));
  g_arrayInterface->AppendElement(arr, NVSEArrayVarInterface::Element(point.y));
  g_arrayInterface->AppendElement(arr, NVSEArrayVarInterface::Element(point.z));
}

void AppendPointArray(NVSEArrayVarInterface::Array *outer, const PathPoint3 &point, Script *scriptObj) {
  auto *pointArr = CreateArray(scriptObj);
  if (!pointArr)
    return;

  AppendPointComponents(pointArr, point);
  g_arrayInterface->AppendElement(outer, NVSEArrayVarInterface::Element(pointArr));
}

bool Cmd_CanPathToRef_Execute(COMMAND_ARGS) {
  *result = 0;

  TESObjectREFR *target = nullptr;
  if (!ExtractArgs(EXTRACT_ARGS, &target))
    return true;

  PathResult path;
  if (GetPath(thisObj, target, path))
    *result = 1;

  return true;
}

bool Cmd_GetPathDistanceToRef_Execute(COMMAND_ARGS) {
  *result = -1.0;

  TESObjectREFR *target = nullptr;
  if (!ExtractArgs(EXTRACT_ARGS, &target))
    return true;

  PathResult path;
  if (GetPath(thisObj, target, path))
    *result = path.distance;

  return true;
}

bool Cmd_GetPathNodeCount_Execute(COMMAND_ARGS) {
  *result = 0;

  TESObjectREFR *target = nullptr;
  if (!ExtractArgs(EXTRACT_ARGS, &target))
    return true;

  PathResult path;
  if (GetPath(thisObj, target, path))
    *result = static_cast<double>(path.nodes.size());

  return true;
}

bool Cmd_GetNthPathNode_Execute(COMMAND_ARGS) {
  *result = 0;

  TESObjectREFR *target = nullptr;
  UINT32 index = 0;
  auto *arr = CreateArray(scriptObj);

  if (ExtractArgs(EXTRACT_ARGS, &target, &index)) {
    PathResult path;
    if (GetPath(thisObj, target, path) && index < path.nodes.size() && arr)
      AppendPointComponents(arr, path.nodes[index]);
  }

  AssignArray(arr, result);
  return true;
}

bool Cmd_GetPathToRef_Execute(COMMAND_ARGS) {
  *result = 0;

  TESObjectREFR *target = nullptr;
  auto *arr = CreateArray(scriptObj);

  if (ExtractArgs(EXTRACT_ARGS, &target)) {
    PathResult path;
    if (GetPath(thisObj, target, path) && arr) {
      for (const auto &node : path.nodes)
        AppendPointArray(arr, node, scriptObj);
    }
  }

  AssignArray(arr, result);
  return true;
}

// JohnnyGuitar section:

static float Sign(const NiPoint3 &p1, const NiPoint3 &p2, const NiPoint3 &p3) {
  return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

static bool PointInTriangle(const NiPoint3 &pt, const NiPoint3 &v1, const NiPoint3 &v2, const NiPoint3 &v3) {
  bool b1 = Sign(pt, v1, v2) < 0.0;
  bool b2 = Sign(pt, v2, v3) < 0.0;
  bool b3 = Sign(pt, v3, v1) < 0.0;

  return (b1 == b2) && (b2 == b3);
}

static NiPoint3 __fastcall GetTriangleCenter(const NiPoint3 &v1, const NiPoint3 &v2, const NiPoint3 &v3) {
  return NiPoint3((v1.x + v2.x + v3.x) / 3.0f, (v1.y + v2.y + v3.y) / 3.0f, (v1.z + v2.z + v3.z) / 3.0f);
}

void __fastcall GetClosestNavMeshTriangle(const TESObjectCELL *apCell, const NiPoint3 &arPointToTest,
                                          bool checkDisabled, float zLimit, NiPoint4 &arOut) {
  NavMeshArray *pNavMeshArray = apCell->pNavMeshes;
  if (!pNavMeshArray)
    return;

  for (uint32_t i = 0; i < pNavMeshArray->GetSize(); i++) {

    NavMeshPtr spNavMesh = pNavMeshArray->GetAt(i);
    if (!spNavMesh)
      continue;

    NavMeshInfo *pInfo = spNavMesh->pNavMeshInfo;
    if (!pInfo)
      continue;

    for (uint32_t j = 0; j < spNavMesh->GetTriangleCount(); j++) {
      NavMeshTriangle *pNavMeshTriangle = spNavMesh->GetTriangle(j);
      if (checkDisabled && ((pNavMeshTriangle->uiFlags & NavMeshTriangle::DISABLED) != 0))
        continue;

      // Get triangle vertices
      NiPoint3 kVerts[3];
      for (uint32_t k = 0; k < 3; k++) {
        NiPoint3 *pVertex = spNavMesh->GetVertex(pNavMeshTriangle->sVertices[k]);
        if (!pVertex)
          continue;

        kVerts[k] = *pVertex;
      }

      NiPoint3 kTriCenter = GetTriangleCenter(kVerts[0], kVerts[1], kVerts[2]);

      if (zLimit > 0 && fabs(kTriCenter.z - arPointToTest.z) > zLimit)
        continue;

      // Get distance to triangle center
      float fDist = arPointToTest.Distance(kTriCenter);

      if (fDist < arOut.w) {
        arOut.w = fDist;
        arOut.x = kTriCenter.x;
        arOut.y = kTriCenter.y;
        arOut.z = kTriCenter.z;
      }
    }
  }
}

// Added for neuro-FNV: walks the cell's navmeshes like the two functions around it.
void TriangleCentresTouching(const TESObjectCELL *cell, float centreX, float centreY, float radius, float z,
                             float zLimit, std::vector<PathPoint3> &out) {
  NavMeshArray *navMeshes = cell ? cell->pNavMeshes : nullptr;
  if (!navMeshes)
    return;
  for (uint32_t i = 0; i < navMeshes->GetSize(); i++) {
    NavMeshPtr navMesh = navMeshes->GetAt(i);
    if (!navMesh)
      continue;
    for (uint32_t j = 0; j < navMesh->GetTriangleCount(); j++) {
      NavMeshTriangle *triangle = navMesh->GetTriangle(j);
      if (!triangle)
        continue;
      PathProbe::Point3 corners[3];
      bool complete = true;
      for (uint32_t k = 0; k < 3; k++) {
        NiPoint3 *vertex = navMesh->GetVertex(triangle->sVertices[k]);
        if (!vertex) {
          complete = false;
          break;
        }
        corners[k] = {vertex->x, vertex->y, vertex->z};
      }
      if (!complete)
        continue;
      const PathPoint3 centre = {(corners[0].x + corners[1].x + corners[2].x) / 3.0f,
                                 (corners[0].y + corners[1].y + corners[2].y) / 3.0f,
                                 (corners[0].z + corners[1].z + corners[2].z) / 3.0f};
      if (std::abs(centre.z - z) > zLimit)
        continue;
      if (PathProbe::TriangleTouchesCircle(corners[0], corners[1], corners[2], centreX, centreY, radius))
        out.push_back(centre);
    }
  }
}

bool __fastcall GetPointNavMesh(const TESObjectCELL *apCell, const NiPoint3 &arPointToTest, bool checkDisabled,
                                float zLimit, NiPoint4 &arOut) {
  NavMeshArray *pNavMeshArray = apCell->pNavMeshes;
  if (!pNavMeshArray)
    return false;

  for (uint32_t i = 0; i < pNavMeshArray->GetSize(); i++) {

    NavMeshPtr spNavMesh = pNavMeshArray->GetAt(i);
    if (!spNavMesh)
      continue;

    NavMeshInfo *pInfo = spNavMesh->pNavMeshInfo;
    if (!pInfo)
      continue;

    for (uint32_t j = 0; j < spNavMesh->GetTriangleCount(); j++) {
      NavMeshTriangle *pNavMeshTriangle = spNavMesh->GetTriangle(j);
      if (!pNavMeshTriangle)
        continue;
      if (checkDisabled && (pNavMeshTriangle->uiFlags & NavMeshTriangle::DISABLED) != 0)
        continue;

      // Get triangle vertices
      NiPoint3 kVerts[3];
      for (uint32_t k = 0; k < 3; k++) {
        NiPoint3 *pVertex = spNavMesh->GetVertex(pNavMeshTriangle->sVertices[k]);
        if (!pVertex)
          continue;

        kVerts[k] = *pVertex;
      }

      // Check if player is inside the triangle
      if (PointInTriangle(arPointToTest, kVerts[0], kVerts[1], kVerts[2])) {
        // Get triangle center
        NiPoint3 kTriCenter = GetTriangleCenter(kVerts[0], kVerts[1], kVerts[2]);

        if (zLimit > 0 && fabs(kTriCenter.z - arPointToTest.z) > zLimit)
          continue;

        // Get distance to triangle center
        float fDist = arPointToTest.Distance(kTriCenter);

        arOut.x = kTriCenter.x;
        arOut.y = kTriCenter.y;
        arOut.z = kTriCenter.z;
        arOut.w = fDist;

        return true;
      }
    }
  }
  return false;
}

} // namespace Pathing
