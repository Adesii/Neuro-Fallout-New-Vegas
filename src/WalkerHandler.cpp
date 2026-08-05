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
namespace Walker {

CREATE_PLUGINSCRIPT(SetAutoMove, float, walk);

TESObjectREFR *g_currentObjectiveTarget;

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

TESObjectREFR *GetCurrentObjectiveTarget() { return g_currentObjectiveTarget; }

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

void Process() {
  g_currentObjectiveTarget = findcurrentobjectivetarget();
  auto player = PlayerCharacter::GetSingleton();
  if (!player || !g_currentObjectiveTarget) {
    SetPlayerAutoMove(player, false);
    return;
  }

  auto *playerRef = reinterpret_cast<TESObjectREFR *>(player);
  targetPosition = GetNthPathPoint(playerRef, g_currentObjectiveTarget, 1);
  if ((targetPosition.x == 0.0f && targetPosition.y == 0.0f && targetPosition.z == 0.0f) ||
      (Math::GetDistance2D(&playerRef->GetPos(), &g_currentObjectiveTarget->GetPos()) < 60.0f)) {
    targetPosition = {g_currentObjectiveTarget->pos.x, g_currentObjectiveTarget->pos.y,
                      g_currentObjectiveTarget->pos.z};
  }

  // _MESSAGE("Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  targetFacingAngle = Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(player->rot.z),
                                                    targetPosition.x, targetPosition.y);
  targetFacingAngle = targetFacingAngle / 10.0f;
  playerFacingAngle = player->rot.z + targetFacingAngle;
  player->rot.z = playerFacingAngle;
  // _MESSAGE("Player Facing Angle: %f, with targetFacingAngle: %f", playerFacingAngle, targetFacingAngle);

  SetPlayerAutoMove(player, true);
  if (Math::GetDistance2D(&playerRef->GetPos(), &g_currentObjectiveTarget->GetPos()) < 60.0f)
    SetPlayerAutoMove(player, false);
}

} // namespace Walker
