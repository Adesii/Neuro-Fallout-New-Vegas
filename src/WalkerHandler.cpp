#include "WalkerHandler.hpp"
#include "CachedScripts.hpp"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "defs/Player.h"
#include "itr/PathingCommands.h"
#include "itr/PathingShared.h"
#include "nvse/GameForms.h"
#include "utils/math.h"
#include <algorithm>
namespace Walker {

CREATE_PLUGINSCRIPT(SetAutoMove, float, walk);

TESObjectREFR *g_currentObjectiveTarget;
TESObjectREFR *g_lookAtTarget;
bool MovePlayerToTarget = true;

TESObjectREFR *findcurrentobjectivetarget() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return nullptr;

  auto *playerRef = reinterpret_cast<TESObjectREFR *>(player);
  // _MESSAGE("Finding current objective target for player: %s", playerRef->GetFullName());
  if (!player->activeQuest || player->questTargetList.Empty()) {
    // _MESSAGE("Player has no active quests.");
    return nullptr;
  }

  for (auto iter = player->questTargetList.Begin(); !iter.End(); ++iter) {
    auto *target = iter.Get() ? iter.Get()->target : nullptr;
    // _MESSAGE("Checking objective target: %s", target ? target->GetFullName() : "None");
    if (target)
      return target;
  }

  return nullptr;
}

float targetFacingAngle = 0.0f;
float playerFacingAngle = 0.0f;
PathPoint3 targetPosition = {0.0f, 0.0f, 0.0f};

PathPoint3 GetNthPathPoint(TESObjectREFR *actorRef, TESObjectREFR *target, int n) {
  Pathing::PathResult pathResult;
  if (Pathing::GetPath(actorRef, target, pathResult)) {
    if (n >= 0 && n < pathResult.nodes.size()) {
      return pathResult.nodes[n];
    }
  }
  return {0.0f, 0.0f, 0.0f}; // Return a default position if the path is not found or n is out of bounds
}

void getNearbyActors(TESObjectREFR *playerRef, float radius, std::vector<Actor *> &nearbyActors) {
  nearbyActors.clear();
  if (!playerRef)
    return;

  auto *cell = playerRef->parentCell;
  if (!cell)
    return;

  auto objects = cell->objectList.GetHead();
  while (objects) {
    objects = objects->GetNext();
    if (objects == nullptr)
      continue;
    auto *ref = objects->GetItem();
    if (ref && ref != playerRef && ref->IsActor()) {
      float distance = Math::GetDistance2D(&playerRef->GetPos(), &ref->GetPos());
      if (distance <= radius) {
        nearbyActors.push_back(reinterpret_cast<Actor *>(ref));
      }
    }
  }
  // Sort the nearby actors by distance to the player
  std::sort(nearbyActors.begin(), nearbyActors.end(), [playerRef](Actor *a, Actor *b) {
    float distanceA = Math::GetDistance2D(&playerRef->GetPos(), &a->GetPos());
    float distanceB = Math::GetDistance2D(&playerRef->GetPos(), &b->GetPos());
    return distanceA < distanceB;
  });
}

void FigureOutCurrentLookAtObject() {
  auto *player = PlayerCharacter::GetSingleton();
  if (g_currentObjectiveTarget) {
    g_lookAtTarget = g_currentObjectiveTarget;
    MovePlayerToTarget = Math::GetDistance2D(&player->GetPos(), &g_lookAtTarget->GetPos()) >= 60.0f;
    return;
  }
  if (g_lookAtTarget) {
    MovePlayerToTarget = Math::GetDistance2D(&player->GetPos(), &g_lookAtTarget->GetPos()) >= 60.0f;
  }
  std::vector<Actor *> nearbyActors;
  // If there is a speaker talking to the player, we look at them once we don't have an objective
  getNearbyActors(reinterpret_cast<TESObjectREFR *>(player), 600.0f, nearbyActors);
  for (auto *actor : nearbyActors) {
    if (actor->IsActor()) {
      g_lookAtTarget = reinterpret_cast<TESObjectREFR *>(actor);
      MovePlayerToTarget = false;
      return;
    }
  }
}

void Process() {
  g_currentObjectiveTarget = findcurrentobjectivetarget();
  auto player = PlayerCharacter::GetSingleton();
  if (!player)
    return;
  FigureOutCurrentLookAtObject();
  if (!g_lookAtTarget)
    return;

  auto *playerRef = reinterpret_cast<TESObjectREFR *>(player);
  targetPosition = GetNthPathPoint(playerRef, g_lookAtTarget, 1);
  if ((targetPosition.x == 0.0f && targetPosition.y == 0.0f && targetPosition.z == 0.0f) ||
      (Math::GetDistance2D(&playerRef->GetPos(), &g_lookAtTarget->GetPos()) < 60.0f)) {
    targetPosition = {g_lookAtTarget->pos.x, g_lookAtTarget->pos.y, g_lookAtTarget->pos.z};
  }

  // _MESSAGE("Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  targetFacingAngle = Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(player->rot.z),
                                                    targetPosition.x, targetPosition.y);
  targetFacingAngle = targetFacingAngle / 10.0f;
  playerFacingAngle = player->rot.z + targetFacingAngle;
  player->rot.z = playerFacingAngle;
  // _MESSAGE("Player Facing Angle: %f, with targetFacingAngle: %f", playerFacingAngle, targetFacingAngle);

  SetPlayerAutoMove(player, MovePlayerToTarget);
}

} // namespace Walker
