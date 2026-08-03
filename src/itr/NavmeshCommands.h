
#pragma once

#include "GameObjects.h"
#include "itr/PathingShared.h"
namespace {

struct EdgeEndpoints {
  const PathPoint3 *p0;
  const PathPoint3 *p1;
};

struct CoverPoint {
  PathPoint3 pos;
  UINT32 flags;
  float distSq;
};

bool PointOnNavmesh(const PathPoint3 &point, TESObjectCELL *cell, TESWorldSpace *worldSpace);
} // namespace
