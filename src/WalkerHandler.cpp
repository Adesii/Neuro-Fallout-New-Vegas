#include "WalkerHandler.hpp"
#include "GameData.h"
#include "GameOSDepend.h"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "PluginAPI.h"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "decoding.h"
#include "defs/Player.h"
#include "hooks/Hooks_DirectInput8Create.h"
#include "itr/PathingCommands.h"
#include "nvse/GameForms.h"
#include "nvse/GameUI.h"
#include "utils/math.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace Walker {
namespace {

constexpr float kInteractionDistance = 80.0f;
constexpr float kNearbyActorDistance = 600.0f;
constexpr float kStuckDistance = 1.0f;
constexpr float kStuckDelay = 3.0f;
constexpr float kRecoveryDuration = 1.0f;
constexpr float kInteractionDelay = 1.0f;
constexpr float kLookTargetResponse = 4.0f;
constexpr float kMaxTurnRate = 3.0f;
constexpr float kTurnSpeedResponse = 4.0f;
constexpr float kTurnAccelerationResponse = 7.0f;
constexpr float kTurnReversalResponse = 2.5f;
constexpr float kTurnDeadZone = 0.01f;
constexpr UINT8 kUnboundKey = 0xFF;

struct TargetSelection {
  BGSQuestObjective::Target *objectiveData = nullptr;
  TESObjectREFR *objective = nullptr;
  TESObjectREFR *lookAt = nullptr;
  bool shouldMove = false;
};

struct WalkerState {
  TESObjectREFR *waypointMarker = nullptr;
  TESObjectREFR *trackedTarget = nullptr;
  TESObjectCELL *trackedCell = nullptr;
  NiPoint3 smoothedLookTarget = {0.0f, 0.0f, 0.0f};
  NiPoint3 lastPlayerPosition = {0.0f, 0.0f, 0.0f};
  float stationaryTime = 0.0f;
  float recoveryTime = 0.0f;
  float interactionTime = 0.0f;
  float turnVelocity = 0.0f;
  bool lookTargetInitialized = false;
  bool positionInitialized = false;
  bool recovering = false;
  UINT8 heldMovementKeys[4] = {kUnboundKey, kUnboundKey, kUnboundKey, kUnboundKey};
};

WalkerState g_state;

bool IsInteractable(TESObjectREFR *ref) {
  if (!ref || ref->GetDeleted() || !ref->baseForm)
    return false;

  const UINT8 refType = ref->eFormType;
  if (refType == _FormType::Character || refType == _FormType::Creature)
    return true;

  const UINT8 typeID = ref->baseForm->eFormType;
  if (typeID == _FormType::TESObjectDOOR)
    return true;
  if (typeID == _FormType::BGSIdleMarker)
    return false;
  if (ref->extraDataList.GetExtraData(_ExtraDataType::ExtraPrimitive))
    return false;

  const char *name = ref->GetFullName();
  return name && name[0] != '\0';
}

float GetFrameTime() {
  auto *time = TimeGlobal::Get();
  return time ? std::clamp(time->secondsPassed, 0.0f, 0.1f) : 0.0f;
}

bool IsValidPoint(const PathPoint3 &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

void ReleaseMovementKeys() {
  auto &input = DIHookControl::GetSingleton();
  for (UINT8 &key : g_state.heldMovementKeys) {
    if (key != kUnboundKey)
      input.SetKeyHeldState(key, false);
    key = kUnboundKey;
  }
}

void ResetNavigationState() {
  g_state.trackedTarget = nullptr;
  g_state.trackedCell = nullptr;
  g_state.stationaryTime = 0.0f;
  g_state.recoveryTime = 0.0f;
  g_state.interactionTime = 0.0f;
  g_state.turnVelocity = 0.0f;
  g_state.lookTargetInitialized = false;
  g_state.positionInitialized = false;
  g_state.recovering = false;
}

void StopInternal(PlayerCharacter *player) {
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
  ResetNavigationState();
}

bool EnsureWaypointMarker(PlayerCharacter *player) {
  if (!g_state.waypointMarker)
    g_state.waypointMarker = TESObjectREFR::Create(true);
  if (!g_state.waypointMarker)
    return false;

  g_state.waypointMarker->parentCell = player->GetParentCell();
  return true;
}

BGSQuestObjective::Target *FindCurrentObjectiveTarget(PlayerCharacter *player) {
  if (!player->activeQuest || player->questTargetList.Empty())
    return nullptr;

  for (auto iter = player->questTargetList.Begin(); !iter.End(); ++iter) {
    auto *target = iter.Get();
    if (target && target->target)
      return target;
  }
  return nullptr;
}

bool IsInSameTravelSpace(TESObjectREFR *a, TESObjectREFR *b) {
  if (!a || !b)
    return false;

  auto *aCell = a->GetParentCell();
  auto *bCell = b->GetParentCell();
  if (!aCell || !bCell)
    return false;
  if (aCell == bCell)
    return true;
  return aCell->worldSpace && aCell->worldSpace == bCell->worldSpace;
}

TESObjectREFR *ResolveObjectiveTarget(PlayerCharacter *player, BGSQuestObjective::Target *targetData) {
  if (!targetData || !targetData->target)
    return nullptr;
  if (IsInSameTravelSpace(player, targetData->target))
    return targetData->target;

  // The engine orders this chain from the player side to the target side; see
  // FalloutNVAccess/src/menus/MapMenuHandler.cpp (MIT) for the same interpretation.
  if (targetData->data.teleportLinks.IsEmpty() || !targetData->data.teleportLinks.pBuffer)
    return nullptr;
  auto *nextDoor = targetData->data.teleportLinks.GetAt(0).door;
  return nextDoor && !nextDoor->GetDeleted() ? nextDoor : nullptr;
}

void GetNearbyObjects(TESObjectREFR *playerRef, float radius, std::vector<TESObjectREFR *> &nearbyObjects,
                      bool actorsOnly) {
  nearbyObjects.clear();
  auto *cell = TES::GetSingleton()->currentInterior;
  if (!cell)
    cell = playerRef->GetParentCell();
  if (!cell)
    return;

  for (auto *entry = cell->objectList.GetHead(); entry; entry = entry->GetNext()) {
    auto *ref = entry->GetItem();
    if (!ref || ref == playerRef || (actorsOnly && !ref->IsActor()))
      continue;
    if (Math::GetDistance2D(&playerRef->GetPos(), &ref->GetPos()) <= radius)
      nearbyObjects.push_back(ref);
  }

  std::sort(nearbyObjects.begin(), nearbyObjects.end(), [playerRef](TESObjectREFR *a, TESObjectREFR *b) {
    return Math::GetDistance2D(&playerRef->GetPos(), &a->GetPos()) <
           Math::GetDistance2D(&playerRef->GetPos(), &b->GetPos());
  });
}

TargetSelection SelectTarget(PlayerCharacter *player) {
  TargetSelection selection;
  selection.objectiveData = FindCurrentObjectiveTarget(player);
  selection.objective = ResolveObjectiveTarget(player, selection.objectiveData);
  if (selection.objective) {
    selection.lookAt = selection.objective;
    selection.shouldMove =
        Math::GetDistance2D(&player->GetPos(), &selection.objective->GetPos()) >= kInteractionDistance;
    return selection;
  }

  std::vector<TESObjectREFR *> nearbyActors;
  GetNearbyObjects(reinterpret_cast<TESObjectREFR *>(player), kNearbyActorDistance, nearbyActors, true);
  if (!nearbyActors.empty())
    selection.lookAt = nearbyActors.front();
  return selection;
}

TESObjectCELL *GetNavigationCell(PlayerCharacter *player) {
  auto *cell = TES::GetSingleton()->currentInterior;
  return cell ? cell : player->GetParentCell();
}

bool BuildPathPoint(PlayerCharacter *player, const NiPoint3 &destination, PathPoint3 &pathPoint) {
  g_state.waypointMarker->pos = destination;

  Pathing::PathResult path;
  if (!Pathing::BuildPath(player, g_state.waypointMarker, path) || path.nodes.empty())
    return false;

  const size_t pointIndex = std::min<size_t>(1, path.nodes.size() - 1);
  pathPoint = path.nodes[pointIndex];
  return IsValidPoint(pathPoint);
}

PathPoint3 ResolveNavigationPoint(PlayerCharacter *player, TESObjectREFR *target) {
  const NiPoint3 destination = target->GetPos();
  if (Math::GetDistance2D(&player->GetPos(), &destination) < kInteractionDistance)
    return {destination.x, destination.y, destination.z};

  PathPoint3 pathPoint;
  if (BuildPathPoint(player, destination, pathPoint))
    return pathPoint;

  auto *cell = GetNavigationCell(player);
  if (cell) {
    NiPoint4 closest = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    Pathing::GetClosestNavMeshTriangle(cell, destination, false, 0.0f, closest);
    if (closest.x != FLT_MAX) {
      const NiPoint3 onNavmesh = {closest.x, closest.y, closest.z};
      if (BuildPathPoint(player, onNavmesh, pathPoint))
        return pathPoint;
    }
  }

  return {destination.x, destination.y, destination.z};
}

void UpdateStuckState(PlayerCharacter *player, bool shouldMove, float deltaTime) {
  if (!shouldMove) {
    g_state.stationaryTime = 0.0f;
    g_state.recoveryTime = 0.0f;
    g_state.recovering = false;
    g_state.positionInitialized = false;
    return;
  }

  if (!g_state.positionInitialized) {
    g_state.lastPlayerPosition = player->GetPos();
    g_state.positionInitialized = true;
    return;
  }

  if (Math::GetDistance2D(&player->GetPos(), &g_state.lastPlayerPosition) <= kStuckDistance)
    g_state.stationaryTime += deltaTime;
  else
    g_state.stationaryTime = 0.0f;

  g_state.lastPlayerPosition = player->GetPos();
  if (!g_state.recovering && g_state.stationaryTime >= kStuckDelay) {
    g_state.recovering = true;
    g_state.recoveryTime = 0.0f;
  }
  if (g_state.recovering) {
    g_state.recoveryTime += deltaTime;
    if (g_state.recoveryTime >= kRecoveryDuration) {
      g_state.recovering = false;
      g_state.stationaryTime = 0.0f;
    }
  }
}

NiPoint3 UpdateSmoothedLookTarget(const PathPoint3 &navigationPoint, float deltaTime) {
  const NiPoint3 desired = {navigationPoint.x, navigationPoint.y, navigationPoint.z};
  if (!g_state.lookTargetInitialized) {
    g_state.smoothedLookTarget = desired;
    g_state.lookTargetInitialized = true;
    return desired;
  }

  const float alpha = 1.0f - std::exp(-kLookTargetResponse * deltaTime);
  g_state.smoothedLookTarget.x += (desired.x - g_state.smoothedLookTarget.x) * alpha;
  g_state.smoothedLookTarget.y += (desired.y - g_state.smoothedLookTarget.y) * alpha;
  g_state.smoothedLookTarget.z += (desired.z - g_state.smoothedLookTarget.z) * alpha;
  return g_state.smoothedLookTarget;
}

void UpdateLook(PlayerCharacter *player, const NiPoint3 &lookTarget, float deltaTime) {
  if (deltaTime <= 0.0f)
    return;

  const float headingError = Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(player->rot.z),
                                                           lookTarget.x, lookTarget.y);
  if (std::abs(headingError) < kTurnDeadZone && std::abs(g_state.turnVelocity) < kTurnDeadZone) {
    g_state.turnVelocity = 0.0f;
    return;
  }

  const float desiredVelocity = std::clamp(headingError * kTurnSpeedResponse, -kMaxTurnRate, kMaxTurnRate);
  const bool reversing = desiredVelocity * g_state.turnVelocity < 0.0f;
  const float response = reversing ? kTurnReversalResponse : kTurnAccelerationResponse;
  const float velocityAlpha = 1.0f - std::exp(-response * deltaTime);
  g_state.turnVelocity += (desiredVelocity - g_state.turnVelocity) * velocityAlpha;

  float turnAmount = g_state.turnVelocity * deltaTime;
  if (turnAmount * headingError > 0.0f && std::abs(turnAmount) > std::abs(headingError)) {
    turnAmount = headingError;
    g_state.turnVelocity = 0.0f;
  }
  player->rot.z += turnAmount;
}

void HoldMovementKey(UINT8 key, size_t slot) {
  if (key == kUnboundKey)
    return;
  DIHookControl::GetSingleton().SetKeyHeldState(key, true);
  g_state.heldMovementKeys[slot] = key;
}

void UpdateMovement(PlayerCharacter *player, const PathPoint3 &navigationPoint, bool shouldMove) {
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
  if (!shouldMove)
    return;

  auto *input = OSInputGlobals::GetSingleton();
  if (!input)
    return;

  const float relativeHeading = Math::GetHeadingBetweenPoints(
      player->pos.x, player->pos.y, Math::ToDegrees(player->rot.z), navigationPoint.x, navigationPoint.y);
  const float diagonalThreshold = Math::ToRadians(22.5f);
  const float reverseThreshold = Math::ToRadians(112.5f);
  const float sideLimit = Math::ToRadians(157.5f);

  if (std::abs(relativeHeading) < Math::ToRadians(67.5f))
    HoldMovementKey(input->keyBinds[ControlCode::Forward], 0);
  if (std::abs(relativeHeading) > reverseThreshold)
    HoldMovementKey(input->keyBinds[ControlCode::Backward], 1);
  if (relativeHeading > diagonalThreshold && relativeHeading < sideLimit)
    HoldMovementKey(input->keyBinds[ControlCode::Right], 2);
  if (relativeHeading < -diagonalThreshold && relativeHeading > -sideLimit)
    HoldMovementKey(input->keyBinds[ControlCode::Left], 3);
}

void TryActivate(PlayerCharacter *player, const TargetSelection &selection, float deltaTime) {
  if (!selection.objective ||
      Math::GetDistance2D(&player->GetPos(), &selection.objective->GetPos()) >= kInteractionDistance) {
    g_state.interactionTime = 0.0f;
    return;
  }

  g_state.interactionTime += deltaTime;
  if (g_state.interactionTime < kInteractionDelay)
    return;
  g_state.interactionTime = 0.0f;

  TESObjectREFR *activationTarget = IsInteractable(selection.objective) ? selection.objective : nullptr;
  if (!activationTarget) {
    std::vector<TESObjectREFR *> nearbyObjects;
    GetNearbyObjects(reinterpret_cast<TESObjectREFR *>(player), 100.0f, nearbyObjects, false);
    auto result = std::find_if(nearbyObjects.begin(), nearbyObjects.end(), IsInteractable);
    if (result != nearbyObjects.end())
      activationTarget = *result;
  }

  if (activationTarget) {
    CALL_MEMBER_FN(activationTarget, Activate)(player, 0, 0, 1);
    _MESSAGE("Walker interacted with: %s", activationTarget->GetFullName());
  }
}

} // namespace

void Stop() { StopInternal(PlayerCharacter::GetSingleton()); }

void Process() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player || StartMenu::Get() ||
      (player->pcControlFlags & (PlayerCharacter::kControlFlag_Movement | PlayerCharacter::kControlFlag_Look)) != 0) {
    StopInternal(player);
    return;
  }
  if (!EnsureWaypointMarker(player)) {
    StopInternal(player);
    return;
  }

  const TargetSelection selection = SelectTarget(player);
  if (!selection.lookAt || selection.lookAt->GetDeleted()) {
    StopInternal(player);
    return;
  }

  auto *currentCell = player->GetParentCell();
  if (selection.lookAt != g_state.trackedTarget || currentCell != g_state.trackedCell) {
    ReleaseMovementKeys();
    ResetNavigationState();
    g_state.trackedTarget = selection.lookAt;
    g_state.trackedCell = currentCell;
  }

  const float deltaTime = GetFrameTime();
  const PathPoint3 navigationPoint = ResolveNavigationPoint(player, selection.lookAt);
  UpdateStuckState(player, selection.shouldMove, deltaTime);

  PathPoint3 movementPoint = navigationPoint;
  if (g_state.recovering) {
    auto *cell = GetNavigationCell(player);
    NiPoint4 closest = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    if (cell && Pathing::GetPointNavMesh(cell, g_state.waypointMarker->GetPos(), false, 0.0f, closest))
      movementPoint = {closest.x, closest.y, closest.z};
  }

  const NiPoint3 lookTarget =
      selection.shouldMove ? UpdateSmoothedLookTarget(movementPoint, deltaTime) : selection.lookAt->GetPos();
  UpdateLook(player, lookTarget, deltaTime);
  UpdateMovement(player, movementPoint, selection.shouldMove);
  TryActivate(player, selection, deltaTime);
}

} // namespace Walker
