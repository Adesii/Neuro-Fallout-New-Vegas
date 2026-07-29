#include "WalkerHandler.hpp"
#include "CachedScripts.hpp"
#include "GameObjects.h"
#include "NiPoint.h"
#include "common.hpp"
#include "common/IDebugLog.h"
#include "defs/Player.h"
#include "itr/PathingCommands.h"
#include "itr/PathingShared.h"
#include "nvse/GameForms.h"
#include "utils/math.h"
namespace Walker {

CREATE_PLUGINSCRIPT(SetAutoMove, float, walk);

TESObjectREFR *g_currentObjectiveTarget;

TESObjectREFR *findcurrentobjectivetarget() {
  auto player = PlayerCharacter::GetSingleton();
  if (!player)
    return nullptr;
  _MESSAGE("Finding current objective target for player: %s", player->GetName());
  auto targetslist = player->GetCurrentQuestObjectiveTargets();
  if (!player->quest || !targetslist) {
    _MESSAGE("Player has no active quests.");
    return nullptr;
  }
  // Use the built-in xNVSE tList Iterator mechanics
  for (auto iter = targetslist->Begin(); !iter.End(); ++iter) {
    BGSQuestObjective::Target *objective = iter.Get(); // Note: might be iter.Info() depending on header version
    if (objective)
      _MESSAGE("Checking objective target: %s", objective && objective->target ? objective->target->GetName() : "None");

    if (objective && objective->target) {
      // Returns the TESObjectREFR* which automatically casts up to TESForm*
      return objective->target;
    }
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
  if (!g_currentObjectiveTarget) {
    SetPlayerAutoMove(player, false);
    return;
  }

  targetPosition = GetNthPathPoint(player, g_currentObjectiveTarget, 1);
  if ((targetPosition.x == 0.0f && targetPosition.y == 0.0f && targetPosition.z == 0.0f) ||
      (Math::GetDistance2D(player->GetPos(), g_currentObjectiveTarget->GetPos()) < 60.0f)) {
    targetPosition = {g_currentObjectiveTarget->posX, g_currentObjectiveTarget->posY, g_currentObjectiveTarget->posZ};
  }

  _MESSAGE("Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  targetFacingAngle = Math::GetHeadingBetweenPoints(player->posX, player->posY, Math::ToDegrees(player->rotZ),
                                                    targetPosition.x, targetPosition.y);
  targetFacingAngle = targetFacingAngle / 10.0f;
  playerFacingAngle = player->rotZ + targetFacingAngle;
  player->rotZ = playerFacingAngle; // convert to radians
  _MESSAGE("Player Facing Angle: %f, with targetFacingAngle: %f", playerFacingAngle, targetFacingAngle);

  SetPlayerAutoMove(player, true);
  if (Math::GetDistance2D(player->GetPos(), g_currentObjectiveTarget->GetPos()) < 60.0f) {
    SetPlayerAutoMove(player, false);
  }
}

} // namespace Walker
