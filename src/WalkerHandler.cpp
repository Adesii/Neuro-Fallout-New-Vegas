#include "WalkerHandler.hpp"
#include "CachedScripts.hpp"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "PluginAPI.h"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "decoding.h"
#include "defs/Player.h"
#include "itr/PathingCommands.h"
#include "itr/PathingShared.h"
#include "nvse/GameForms.h"
#include "utils/DelayedGuard.hpp"
#include "utils/math.h"
#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <memory>
namespace Walker {

CREATE_PLUGINSCRIPT(SetAutoMove, float, walk);

///////// From FalloutNVAccess plugin ///////// TODO: Move this to a utility file instead. Should be useful elsewhere
/// too
bool IsInteractable(TESObjectREFR *a_ref) {
  if (!a_ref)
    return false;
  if (a_ref->GetDeleted())
    return false;
  // if (a_ref->uiFormFlags & TESObjectREFR::kFlags_Temporary) return false;

  if (!a_ref->baseForm)
    return false;

  // Skip disabled references (not currently active in the world)
  // if (IsRefDisabled(a_ref)) return false;

  // Actor detection uses the REFERENCE type (Character=0x3B, Creature=0x3C),
  // not the base form type (TESNPC=0x2A, TESCreature=0x2B).
  UINT8 refType = a_ref->eFormType;

  // Actors are always interactable (living NPCs and corpses).
  // Check early — actors may not have 3D loaded at distance but are still valid.
  if (refType == _FormType::Character || refType == _FormType::Creature)
    return true;

  UINT8 typeID = a_ref->baseForm->eFormType;

  // Skip "destroyed" world fixtures — a fixture flagged destroyed via SetDestroyed
  // is powered-off / spent and must not be reported:
  //   - Hidden Valley Datastore terminals (BGSTerminal) the power switch flags off.
  //   - Harvested plants (TESObjectACTI, e.g. Broc Flower) whose pick script sets
  //     destroyed until the plant respawns.
  // This is deliberately NOT applied to pickup items. FNV overloads SetDestroyed as
  // a generic script state marker, and some quest ITEMS flag themselves destroyed
  // while still intact and interactive — e.g. the Goodsprings "Back in the Saddle"
  // tutorial target VCG02Bottle (a MISC "Sunset Sarsaparilla Bottle") runs
  // SetDestroyed 1 in OnLoad so its OnHitWith can tally shots, yet it stays a valid
  // shoot-target the whole time. Hiding destroyed items would drop such targets from
  // the Items list, so item types are exempt (an item's "broken" state is condition/
  // ExtraHealth, never this flag — so the exemption loses nothing). Bit 0x00800000 =
  // kFlags_Destroyed (JIP's TESObjectREFR::IsDestroyed). Checked after the actor
  // early-return so it never affects living/dead actors, whose state is lifeState.
  // TODO:
  // if ((a_ref->uiFormFlags & TESObjectREFR::kFlags_Destroyed) && !IsItemType(typeID))
  // 	return false;

  // Doors are always interactable (load doors may lack 3D until approached)
  if (typeID == _FormType::TESObjectDOOR)
    return true;

  // Skip objects with no 3D loaded (not visible in the world)
  // if (!a_ref->GetNiNode())
  //   return false;

  UINT32 baseID = a_ref->baseForm->GetFormID();

  // Skip known internal marker base forms (XMarker, MapMarker, triggers, etc.)
  // if (IsInternalMarkerID(baseID))
  //   return false;

  // Skip BGSIdleMarker — always an internal marker
  if (typeID == _FormType::BGSIdleMarker)
    return false;

  // Skip activators that use the invisible EditorMarker.NIF model
  // if (typeID == _FormType::TESObjectACTI && IsEditorMarkerActivator(a_ref->baseForm))
  //   return false;

  // Skip invisible lights (no model mesh)
  // if (typeID == _FormType::TESObjectLIGH && IsInvisibleLight(a_ref->baseForm))
  //   return false;

  // Skip trigger volume objects (activators/furniture with attached primitive
  // collision volumes, e.g. "Vigor Tester Trigger", "CouchTrigger").
  // These are invisible collision zones used by scripts, not player-interactable.
  if (a_ref->extraDataList.GetExtraData(_ExtraDataType::ExtraPrimitive))
    return false;

  // Must have a display name
  const char *name = a_ref->GetFullName();
  if (!name || name[0] == '\0')
    return false;

  return true;
}

typedef void *(__cdecl *_GameHeapAlloc)(UINT32 size);
static _GameHeapAlloc GameHeapAlloc = (_GameHeapAlloc)0xAA3E40;

TESObjectREFR *m_waypointTarget; // A Custom Temporary Target that can be set
void CreateWaypointMarker() {
  if (m_waypointTarget)
    return; // already created
  m_waypointTarget = TESObjectREFR::Create(true);
  m_waypointTarget->parentCell = PlayerCharacter::GetSingleton()->parentCell;
}

///////////////////////

const float INTERACTION_DISTANCE = 80.0f;

BGSQuestObjective::Target *g_currentObjectiveTargetData;
TESObjectREFR *g_currentObjectiveTarget;
TESObjectREFR *g_lookAtTarget;
bool MovePlayerToTarget = true;

BGSQuestObjective::Target *findcurrentobjectivetarget() {
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
    BGSQuestObjective::Target *target = iter.Get() ? iter.Get() : nullptr;
    // _MESSAGE("Checking objective target: %s", target ? target->GetFullName() : "None");
    if (target && target->target)
      return target;
  }

  return nullptr;
}

float targetFacingAngle = 0.0f;
float playerFacingAngle = 0.0f;
PathPoint3 targetPosition = {0.0f, 0.0f, 0.0f};

PathPoint3 GetNthPathPoint(Actor *actorRef, TESObjectREFR *target, int n) {
  Pathing::PathResult pathResult;
  if (Pathing::BuildPath((Actor *)actorRef, target, pathResult)) {
    if (n >= 0 && n < pathResult.nodes.size()) {
      return pathResult.nodes[n];
    } else if (!pathResult.nodes.empty()) {
      _MESSAGE("Requested path point index %d is out of bounds. Returning last node instead.", n);
      return pathResult.nodes.front(); // Return the last node if n is out of bounds
    }
  }
  return {FLT_MAX, FLT_MAX, FLT_MAX};
}

void getNearbyObjects(TESObjectREFR *playerRef, float radius, std::vector<TESObjectREFR *> &nearbyActors,
                      bool actorsOnly) {
  nearbyActors.clear();
  if (!playerRef)
    return;

  auto *cell = TES::GetSingleton()->currentInterior;
  if (!cell) {
    cell = playerRef->GetParentCell();
    if (!cell)
      return;
  }

  auto objects = cell->objectList.GetHead();
  while (objects) {
    objects = objects->GetNext();
    if (objects == nullptr)
      continue;
    auto *ref = objects->GetItem();
    if (ref && ref != playerRef && (ref->IsActor() || !actorsOnly)) {
      float distance = Math::GetDistance2D(&playerRef->GetPos(), &ref->GetPos());
      if (distance <= radius) {
        nearbyActors.push_back(reinterpret_cast<Actor *>(ref));
      }
    }
  }
  // Sort the nearby actors by distance to the player
  std::sort(nearbyActors.begin(), nearbyActors.end(), [playerRef](TESObjectREFR *a, TESObjectREFR *b) {
    float distanceA = Math::GetDistance2D(&playerRef->GetPos(), &a->GetPos());
    float distanceB = Math::GetDistance2D(&playerRef->GetPos(), &b->GetPos());
    return distanceA < distanceB;
  });
}

void FigureOutCurrentLookAtObject() {
  auto *player = PlayerCharacter::GetSingleton();
  if (g_currentObjectiveTarget) {
    g_lookAtTarget = g_currentObjectiveTarget;
    MovePlayerToTarget = Math::GetDistance2D(&player->GetPos(), &g_lookAtTarget->GetPos()) >= INTERACTION_DISTANCE;
    return;
  }
  if (g_lookAtTarget) {
    MovePlayerToTarget = Math::GetDistance2D(&player->GetPos(), &g_lookAtTarget->GetPos()) >= INTERACTION_DISTANCE;
  }
  std::vector<TESObjectREFR *> nearbyActors;
  // If there is a speaker talking to the player, we look at them once we don't have an objective
  getNearbyObjects(reinterpret_cast<TESObjectREFR *>(player), 600.0f, nearbyActors, true);
  for (auto *actor : nearbyActors) {
    if (actor->IsActor()) {
      g_lookAtTarget = actor;
      MovePlayerToTarget = false;
      return;
    }
  }
}

NiPoint3 lastPlayerPosition = {0.0f, 0.0f, 0.0f};
bool playerIsStuck = false;

TESObjectREFR *GetCurrentObjectiveTarget() {
  if (g_currentObjectiveTargetData) {
    if (TES::GetSingleton()->currentInterior && !g_currentObjectiveTargetData->target->IsInInterior()) {
      for (auto door : g_currentObjectiveTargetData->data.teleportLinks) {
        if (door.door && door.door->GetParentCell() == TES::GetSingleton()->currentInterior) {
          return door.door;
        }
      };
    }
    if (!TES::GetSingleton()->currentInterior && g_currentObjectiveTargetData->target->IsInInterior()) {
      for (auto door : g_currentObjectiveTargetData->data.teleportLinks) {
        if (door.door && !door.door->IsInInterior()) {
          return door.door;
        }
      };
    }
    return g_currentObjectiveTargetData->target;
  }
  return nullptr;
}

// TODO: Add Turn accellaration and deceleration to make the turning more natural. Right now it just snaps to the target
// angle if the angles are too small. making the view jittery
// It should only kick in in rapidly changing angles. if the angle is changing in the same direction its fine if its
// fast
void Process() {
  bool isinStartMenu = StartMenu::Get() != nullptr;
  if (isinStartMenu)
    return;

  CreateWaypointMarker();

  g_currentObjectiveTargetData = findcurrentobjectivetarget();
  g_currentObjectiveTarget = GetCurrentObjectiveTarget();
  auto player = PlayerCharacter::GetSingleton();
  if (!player)
    return;
  m_waypointTarget->parentCell = player->GetParentCell();
  if ((player->pcControlFlags & (PlayerCharacter::kControlFlag_Movement | PlayerCharacter::kControlFlag_Look)) != 0) {
    return;
  }
  FigureOutCurrentLookAtObject();
  if (!g_lookAtTarget)
    return;

  // _MESSAGE("Current Objective Target: %s", g_lookAtTarget ? g_lookAtTarget->GetFullName() : "None");
  // _MESSAGE("Player Position: x=%f, y=%f, z=%f", player->GetPos().x, player->GetPos().y, player->GetPos().z);

  auto *playerRef = reinterpret_cast<TESObjectREFR *>(player);
  TESObjectCELL *TargetCell = TES::GetSingleton()->currentInterior;
  if (!TargetCell) {
    TargetCell = player->GetParentCell();
  }
  m_waypointTarget->pos = g_lookAtTarget->GetPos();
  targetPosition = GetNthPathPoint(player, m_waypointTarget, 1);
  if (Math::GetDistance2D(&playerRef->GetPos(), &m_waypointTarget->GetPos()) < 50.0f) {
    targetPosition = GetNthPathPoint(player, m_waypointTarget, 2);
    _MESSAGE("Player is close to the target, getting the 2nd path point instead of the 1st.");
  }
  // _MESSAGE("1Current Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  NiPoint3 targetNavMeshPoint = {targetPosition.x, targetPosition.y, targetPosition.z};
  if ((targetPosition.x == FLT_MAX || targetPosition.y == FLT_MAX || targetPosition.z == FLT_MAX)) {
    NiPoint4 arOut = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    Pathing::GetClosestNavMeshTriangle(TargetCell, g_lookAtTarget->GetPos(), false, 0.0f, arOut);
    if (arOut.x != FLT_MAX) {
      m_waypointTarget->pos = {arOut.x, arOut.y, arOut.z};
    }
    targetPosition = GetNthPathPoint(player, m_waypointTarget, 1);
    // _MESSAGE("2Current Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  }
  if ((Math::GetDistance2D(&playerRef->GetPos(), &g_lookAtTarget->GetPos()) < INTERACTION_DISTANCE)) {
    targetPosition = {g_lookAtTarget->pos.x, g_lookAtTarget->pos.y, g_lookAtTarget->pos.z};
    // _MESSAGE("3Current Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  }
  NiPoint3 targetNavMeshPoint2 = m_waypointTarget->GetPos();
  NiPoint4 arOut = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
  if (targetPosition.x == FLT_MAX || targetPosition.y == FLT_MAX || targetPosition.z == FLT_MAX) {
    Pathing::GetClosestNavMeshTriangle(TargetCell, targetNavMeshPoint2, false, 0.0f, arOut);
    if (arOut.x != FLT_MAX) {
      m_waypointTarget->pos = {arOut.x, arOut.y, arOut.z};
      targetPosition = GetNthPathPoint(player, m_waypointTarget, 1);
      // _MESSAGE("4Current Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
      if (targetPosition.x == FLT_MAX || targetPosition.y == FLT_MAX || targetPosition.z == FLT_MAX) {
        targetPosition = {g_lookAtTarget->pos.x, g_lookAtTarget->pos.y, g_lookAtTarget->pos.z};
      }
    } else {
      targetPosition = {g_lookAtTarget->pos.x, g_lookAtTarget->pos.y, g_lookAtTarget->pos.z};
    }
  }

  // _MESSAGE("5Current Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  if (MovePlayerToTarget && ((Math::GetDistance2D(&player->GetPos(), &lastPlayerPosition) <= 1.0f &&
                              DelayedGuard::Delay("StuckTimerMovement", 3.0f)) ||
                             playerIsStuck)) {
    playerIsStuck = true;
    TESObjectCELL *TargetCell = TES::GetSingleton()->currentInterior;
    if (!TargetCell) {
      TargetCell = player->GetParentCell();
    }
    if (Pathing::GetPointNavMesh(TargetCell, targetNavMeshPoint2, false, 0.0f, arOut)) {
      _MESSAGE("Current Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
      _MESSAGE("Target NavMesh Point: x=%f, y=%f, z=%f, distance=%f", arOut.x, arOut.y, arOut.z, arOut.w);
      targetPosition.x = arOut.x;
      targetPosition.y = arOut.y;
      targetPosition.z = arOut.z;
      m_waypointTarget->pos = {targetPosition.x, targetPosition.y, targetPosition.z};
      targetPosition = GetNthPathPoint(player, m_waypointTarget, 1);
    } else {
      Pathing::GetClosestNavMeshTriangle(TargetCell, targetNavMeshPoint2, false, 0.0f, arOut);
      if (arOut.w < 0.0f) {
        _MESSAGE("No valid NavMesh triangle found for target position.");
        // Reset the unstuck movement timer to avoid immediate retry
        DelayedGuard::Reset("StuckTimerMovement");
        playerIsStuck = false;
      } else {
        _MESSAGE("Current Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
        _MESSAGE("Closest NavMesh Triangle Point: x=%f, y=%f, z=%f, distance=%f", arOut.x, arOut.y, arOut.z, arOut.w);
        targetPosition.x = arOut.x;
        targetPosition.y = arOut.y;
        targetPosition.z = arOut.z;
        m_waypointTarget->pos = {targetPosition.x, targetPosition.y, targetPosition.z};
        targetPosition = GetNthPathPoint(player, m_waypointTarget, 1);
      }
    }
    // Move one second according to the above points
    if (DelayedGuard::Delay("UnstuckMovementTimer", 1.0f)) {
      playerIsStuck = false;
      DelayedGuard::Reset("StuckTimerMovement");
      DelayedGuard::Reset("UnstuckMovementTimer");
    }
  }
  if (!MovePlayerToTarget) {
    DelayedGuard::Reset("StuckTimerMovement");
    targetPosition = {
        g_lookAtTarget->pos.x, g_lookAtTarget->pos.y,
        g_lookAtTarget->pos.z}; // If we aren't moving to the target, we want to look at it instead of moving to it
  }
  // _MESSAGE("Target Position: x=%f, y=%f, z=%f", targetPosition.x, targetPosition.y, targetPosition.z);
  targetFacingAngle = Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(player->rot.z),
                                                    targetPosition.x, targetPosition.y);
  targetFacingAngle = targetFacingAngle / 10.0f;
  _MESSAGE("Target Facing Angle: %f", targetFacingAngle);
  playerFacingAngle = player->rot.z + targetFacingAngle;
  player->rot.z = playerFacingAngle;
  lastPlayerPosition = player->GetPos();
  // We reached our destination but the objective didn't finish. this likely means we need to interact with something
  if (g_currentObjectiveTarget &&
      Math::GetDistance2D(&player->GetPos(), &g_currentObjectiveTarget->GetPos()) < INTERACTION_DISTANCE) {
    // Scan current cell for interactable objects and see if we can interact with them
    //  Prioritize the nearest one to the player
    //  but first check if the objective target is interactable and within range
    if (IsInteractable(g_currentObjectiveTarget)) {
      // _MESSAGE("test interaction: %b ", g_currentObjectiveTarget->Activate((TESObjectREFR *)player, 0, 0, 1));
      if (DelayedGuard::Delay("InteractionTimer", 1.0f)) {
        CALL_MEMBER_FN(g_currentObjectiveTarget, Activate)(player, 0, 0, 1);
        DelayedGuard::Reset("InteractionTimer");
        _MESSAGE("Interacting with current objective target: %s", g_currentObjectiveTarget->GetFullName());
      }
    } else {
      if (DelayedGuard::Delay("InteractionTimer", 1.0f)) {
        std::vector<TESObjectREFR *> nearbyActors;
        getNearbyObjects(reinterpret_cast<TESObjectREFR *>(player), 100.0f, nearbyActors, false);
        for (auto *ref : nearbyActors) {
          if (IsInteractable(ref)) {
            CALL_MEMBER_FN(ref, Activate)(player, 0, 0, 1);
            DelayedGuard::Reset("InteractionTimer");
            _MESSAGE("Interacting with nearby interactable object: %s", ref->GetFullName());
            break;
          }
        }
      }
    }
  }
  // _MESSAGE("Player Facing Angle: %f, with targetFacingAngle: %f", playerFacingAngle, targetFacingAngle);
  PlayerMover *playerMover = reinterpret_cast<PlayerMover *>(player->actorMover);
  // TODO: Figure out how to move player to target position so we can also strafe if needed
  // Look into how FalloutNVAccess handles this with AutoMover
  if (playerMover) {
    Player::SetAutoMove(player, MovePlayerToTarget);
    // If we aren't moving left. Set the Move left flag so we automatically move
    // if (playerMover->pcMovementFlags & ActorMover::MovementFlags::kMoveFlag_Left) {
    //   playerMover->pcMovementFlags &= ~ActorMover::MovementFlags::kMoveFlag_Left;
    // }
    // Player::veloctyVector direction = {targetPosition.x - player->pos.x * 100.0f,
    //                                    targetPosition.y - player->pos.y * 100.0f,
    //                                    targetPosition.z - player->pos.z * 100.0f};
    // Player::SetVelocity(player, direction);
    // playerMover->Update(TimeGlobal::Get()->secondsPassed);
  } else {

    Player::SetAutoMove(player, MovePlayerToTarget);
  }
}

} // namespace Walker
