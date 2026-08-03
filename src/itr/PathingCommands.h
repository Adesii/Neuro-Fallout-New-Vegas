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
bool GetPath(TESObjectREFR *actorRef, TESObjectREFR *target, PathResult &out);
} // namespace Pathing
