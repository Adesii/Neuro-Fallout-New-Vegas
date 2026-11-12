#pragma once

#include "GameObjects.h"
#include "itr/PathingShared.h"
#include <vector>
namespace Pathing {

struct PathResult {
  bool complete = false;
  float distance = -1.0f;
  std::vector<PathPoint3> nodes;
};
// A place the pathfinder should route around: navmesh triangles whose centre lies within `radius` of `centre` cost
// (1 + cost * (1 - (distance / radius)^2)) times their length in the search. Not part of itr-nvse; see BuildPath.
struct AvoidSphere {
  PathPoint3 centre;
  float radius;
  float cost;
};
bool GetPath(TESObjectREFR *actorRef, TESObjectREFR *target, PathResult &out);
bool BuildPath(Actor *actor, TESObjectREFR *target, PathResult &out, const std::vector<AvoidSphere> *avoid = nullptr);
bool PointOnNavmesh(const PathPoint3 &point, TESObjectCELL *cell, TESWorldSpace *worldSpace);
bool __fastcall GetPointNavMesh(const TESObjectCELL *apCell, const NiPoint3 &arPointToTest, bool checkDisabled,
                                float zLimit, NiPoint4 &arOut);
void __fastcall GetClosestNavMeshTriangle(const TESObjectCELL *apCell, const NiPoint3 &arPointToTest,
                                          bool checkDisabled, float zLimit, NiPoint4 &arOut);
} // namespace Pathing
