// temporary path probes exposed as script commands
// builds a standalone PathingRequest/Solution instead of reading an actor's live mover path

#include "itr/PathingCommands.h"
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

constexpr UInt32 kMaxPathNodes = 1024;

DWORD s_mainThreadId = 0;

bool IsActorRef(TESObjectREFR *ref) {
  if (!ref || !ref->baseForm)
    return false;
  return ref->baseForm->typeID == kFormType_Creature ||
         ref->baseForm->typeID == kFormType_TESNPC; // Was kFormType_NPC idk if this is the correct correction
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

bool BuildPath(Actor *actor, TESObjectREFR *target, PathResult &out) {
  out = PathResult();

  if (!actor || !target || !target->parentCell)
    return false;

  ScopedPathingRequest request;
  ScopedPathingSolution solution;

  PathPoint3 destination = {target->posX, target->posY, target->posZ};
  CdeclCall<void>(0x9DBC90, actor, request.Get(), &destination, target->parentCell, target->parentCell->worldSpace,
                  0.0f, static_cast<void *>(nullptr));

  if (!CdeclCall<bool>(0x6D0900, request.Get(), solution.Get()))
    return false;

  auto *solutionData = solution.Get();
  if (solutionData->incompletePath)
    return false;

  const UInt32 nodeCount = ThisStdCall<UInt32>(0x8B6800, solution.Get());
  const UInt32 safeCount = (std::min)(nodeCount, kMaxPathNodes);
  out.nodes.reserve(safeCount);

  for (UInt32 i = 0; i < safeCount; ++i) {
    auto *node = ThisStdCall<PathingNodeLayout *>(0x6E7970, solution.Get(), i);
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
  UInt32 index = 0;
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
} // namespace Pathing
