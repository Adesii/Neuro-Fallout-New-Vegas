#include "WalkerHandler.hpp"
#include "GameData.h"
#include "GameOSDepend.h"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
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
#include <utility>

namespace Walker {
namespace {

constexpr float kInteractionDistance = 100.0f;
constexpr float kStuckDistance = 1.0f;
constexpr float kStuckDelay = 3.0f;
constexpr float kRecoveryDuration = 1.0f;
constexpr float kInteractionDelay = 1.0f;
constexpr float kCloseLookDistance = 400.0f;
constexpr float kLookAlignmentTolerance = 0.12f;
constexpr float kPlayerEyeHeight = 100.0f;
constexpr int kMaxRecoveryAttempts = 3;
constexpr float kLookTargetResponse = 4.0f;
constexpr float kMaxTurnRate = 3.0f;
constexpr float kTurnSpeedResponse = 4.0f;
constexpr float kTurnAccelerationResponse = 7.0f;
constexpr float kTurnReversalResponse = 2.5f;
constexpr float kTurnDeadZone = 0.01f;
constexpr UINT8 kUnboundKey = 0xFF;

struct Command {
  uint32_t targetFormId = 0;
  Intent intent = Intent::Move;
  Owner owner = Owner::None;
  std::string description;
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
  int recoveryAttempts = 0;
  bool lookTargetInitialized = false;
  bool positionInitialized = false;
  bool recovering = false;
  bool hasCommand = false;
  Command command;
  UINT8 heldMovementKeys[4] = {kUnboundKey, kUnboundKey, kUnboundKey, kUnboundKey};
};

WalkerState g_state;
std::vector<Event> g_events;

TESObjectREFR *LookupReference(uint32_t formId) {
  auto *form = TESForm::GetFormByNumericID(formId);
  if (!form || !form->IsReference())
    return nullptr;
  auto *ref = static_cast<TESObjectREFR *>(form);
  return ref->GetDeleted() || ref->GetDisabled() ? nullptr : ref;
}

bool IsInteractable(TESObjectREFR *ref) {
  if (!ref || !ref->baseForm)
    return false;
  if (ref->IsActor())
    return true;

  const UINT8 type = ref->baseForm->eFormType;
  if (type == _FormType::TESObjectDOOR || type == _FormType::TESObjectCONT || type == _FormType::BGSTerminal ||
      type == _FormType::TESFlora || type == _FormType::TESFurniture)
    return true;
  if (ref->extraDataList.GetExtraData(_ExtraDataType::ExtraPrimitive))
    return false;
  switch (type) {
  case _FormType::TESObjectWEAP:
  case _FormType::TESObjectARMO:
  case _FormType::TESObjectCLOT:
  case _FormType::TESAmmo:
  case _FormType::AlchemyItem:
  case _FormType::IngredientItem:
  case _FormType::TESObjectBOOK:
  case _FormType::BGSNote:
  case _FormType::TESKey:
  case _FormType::TESObjectMISC:
    return true;
  default:
    return false;
  }
}

// FNV's clamped pitch, effective yaw, and camera rotation path. Proven by
// FalloutNVAccess/src/AimUtil.cpp (MIT; notice preserved in src/defs/FalloutNVAccess-license.md).
using SetPitchWithClamp = void(__thiscall *)(Actor *, float);
using AddDeltaYaw = void(__thiscall *)(Actor *, float);
using GetEffectiveYaw = float(__thiscall *)(PlayerCharacter *, int);
const auto kSetPitchWithClamp = reinterpret_cast<SetPitchWithClamp>(0x931D90);
const auto kAddDeltaYaw = reinterpret_cast<AddDeltaYaw>(0x931D30);
const auto kGetEffectiveYaw = reinterpret_cast<GetEffectiveYaw>(0x953F20);
auto *const kCameraPitch = reinterpret_cast<float *>(0x11E0764);
auto *const kCameraYaw = reinterpret_cast<float *>(0x11E076C);

void SetPitch(PlayerCharacter *player, float pitch) {
  if (!player)
    return;
  pitch = std::clamp(pitch, -1.5533f, 1.5533f);
  kSetPitchWithClamp(player, pitch);
  player->rot.x = pitch;
  *kCameraPitch = pitch;
}

float EffectiveYaw(PlayerCharacter *player) { return kGetEffectiveYaw(player, 0); }

void SetYaw(PlayerCharacter *player, float yaw) {
  while (yaw > static_cast<float>(Math::PI))
    yaw -= static_cast<float>(Math::PI * 2.0);
  while (yaw < -static_cast<float>(Math::PI))
    yaw += static_cast<float>(Math::PI * 2.0);
  float delta = yaw - EffectiveYaw(player);
  while (delta > static_cast<float>(Math::PI))
    delta -= static_cast<float>(Math::PI * 2.0);
  while (delta < -static_cast<float>(Math::PI))
    delta += static_cast<float>(Math::PI * 2.0);
  kAddDeltaYaw(player, delta);
  player->rot.z = yaw;
  *kCameraYaw = yaw;
}

float GetFrameTime() {
  auto *time = TimeGlobal::Get();
  return time ? std::clamp(time->secondsPassed, 0.0f, 0.1f) : 0.0f;
}

float Distance3D(const NiPoint3 &left, const NiPoint3 &right) {
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  const float z = left.z - right.z;
  return std::sqrt(x * x + y * y + z * z);
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

void ResetRouteState() {
  g_state.trackedTarget = nullptr;
  g_state.trackedCell = nullptr;
  g_state.stationaryTime = 0.0f;
  g_state.recoveryTime = 0.0f;
  g_state.interactionTime = 0.0f;
  g_state.turnVelocity = 0.0f;
  g_state.recoveryAttempts = 0;
  g_state.lookTargetInitialized = false;
  g_state.positionInitialized = false;
  g_state.recovering = false;
}

void ClearCommand(PlayerCharacter *player) {
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
  ResetRouteState();
  g_state.hasCommand = false;
  g_state.command = {};
}

void FinishCommand(PlayerCharacter *player, EventType type, std::string message) {
  g_events.push_back({type, g_state.command.owner, g_state.command.targetFormId, std::move(message)});
  ClearCommand(player);
}

bool EnsureWaypointMarker(PlayerCharacter *player) {
  if (!g_state.waypointMarker)
    g_state.waypointMarker = TESObjectREFR::Create(true);
  if (!g_state.waypointMarker)
    return false;
  g_state.waypointMarker->parentCell = player->GetParentCell();
  return true;
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
  pathPoint = path.nodes[std::min<size_t>(1, path.nodes.size() - 1)];
  return IsValidPoint(pathPoint);
}

PathPoint3 ResolveNavigationPoint(PlayerCharacter *player, TESObjectREFR *target) {
  const NiPoint3 destination = target->GetPos();
  if (Distance3D(player->GetPos(), destination) < kInteractionDistance)
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

bool UpdateStuckState(PlayerCharacter *player, bool shouldMove, float deltaTime) {
  if (!shouldMove) {
    g_state.stationaryTime = 0.0f;
    g_state.recoveryTime = 0.0f;
    g_state.recovering = false;
    g_state.positionInitialized = false;
    return true;
  }

  if (!g_state.positionInitialized) {
    g_state.lastPlayerPosition = player->GetPos();
    g_state.positionInitialized = true;
    return true;
  }

  if (Math::GetDistance2D(&player->GetPos(), &g_state.lastPlayerPosition) <= kStuckDistance)
    g_state.stationaryTime += deltaTime;
  else
    g_state.stationaryTime = 0.0f;
  g_state.lastPlayerPosition = player->GetPos();

  if (!g_state.recovering && g_state.stationaryTime >= kStuckDelay) {
    if (++g_state.recoveryAttempts > kMaxRecoveryAttempts)
      return false;
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
  return true;
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
  const float currentYaw = EffectiveYaw(player);
  const float headingError = Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(currentYaw),
                                                           lookTarget.x, lookTarget.y);
  if (std::abs(headingError) < kTurnDeadZone && std::abs(g_state.turnVelocity) < kTurnDeadZone) {
    g_state.turnVelocity = 0.0f;
    return;
  }
  const float desiredVelocity = std::clamp(headingError * kTurnSpeedResponse, -kMaxTurnRate, kMaxTurnRate);
  const float response =
      desiredVelocity * g_state.turnVelocity < 0.0f ? kTurnReversalResponse : kTurnAccelerationResponse;
  const float velocityAlpha = 1.0f - std::exp(-response * deltaTime);
  g_state.turnVelocity += (desiredVelocity - g_state.turnVelocity) * velocityAlpha;

  float turnAmount = g_state.turnVelocity * deltaTime;
  if (turnAmount * headingError > 0.0f && std::abs(turnAmount) > std::abs(headingError)) {
    turnAmount = headingError;
    g_state.turnVelocity = 0.0f;
  }
  SetYaw(player, currentYaw + turnAmount);
}

float GetTargetAimHeight(TESObjectREFR *target) {
  if (!target || !target->baseForm)
    return 0.0f;
  if (target->IsActor())
    return 100.0f;
  switch (target->baseForm->eFormType) {
  case _FormType::TESObjectDOOR:
  case _FormType::TESObjectCONT:
  case _FormType::BGSTerminal:
  case _FormType::TESFurniture:
    return 50.0f;
  case _FormType::TESFlora:
    return 20.0f;
  default:
    return 0.0f;
  }
}

float UpdatePitch(PlayerCharacter *player, TESObjectREFR *target, bool closeToTarget, float deltaTime) {
  float desiredPitch = 0.0f;
  if (closeToTarget) {
    const float x = target->pos.x - player->pos.x;
    const float y = target->pos.y - player->pos.y;
    const float z = target->pos.z + GetTargetAimHeight(target) - (player->pos.z + kPlayerEyeHeight);
    desiredPitch = -std::atan2(z, std::sqrt(x * x + y * y));
  }
  const float alpha = 1.0f - std::exp(-kLookTargetResponse * deltaTime);
  const float pitch = *kCameraPitch + (desiredPitch - *kCameraPitch) * alpha;
  SetPitch(player, pitch);
  return std::abs(desiredPitch - pitch);
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

  const float heading = Math::GetHeadingBetweenPoints(
      player->pos.x, player->pos.y, Math::ToDegrees(EffectiveYaw(player)), navigationPoint.x, navigationPoint.y);
  const float diagonalThreshold = Math::ToRadians(22.5f);
  const float reverseThreshold = Math::ToRadians(112.5f);
  const float sideLimit = Math::ToRadians(157.5f);
  if (std::abs(heading) < Math::ToRadians(67.5f))
    HoldMovementKey(input->keyBinds[ControlCode::Forward], 0);
  if (std::abs(heading) > reverseThreshold)
    HoldMovementKey(input->keyBinds[ControlCode::Backward], 1);
  if (heading > diagonalThreshold && heading < sideLimit)
    HoldMovementKey(input->keyBinds[ControlCode::Right], 2);
  if (heading < -diagonalThreshold && heading > -sideLimit)
    HoldMovementKey(input->keyBinds[ControlCode::Left], 3);
}

void TryComplete(PlayerCharacter *player, TESObjectREFR *target, float distance, bool aligned, float deltaTime) {
  if (distance >= kInteractionDistance) {
    g_state.interactionTime = 0.0f;
    return;
  }
  ReleaseMovementKeys();
  if (g_state.command.intent == Intent::Move) {
    FinishCommand(player, EventType::Completed, "Reached " + g_state.command.description + ".");
    return;
  }

  if (!aligned) {
    g_state.interactionTime = 0.0f;
    return;
  }
  g_state.interactionTime += deltaTime;
  if (g_state.interactionTime < kInteractionDelay)
    return;
  if (!IsInteractable(target)) {
    FinishCommand(player, EventType::Failed, g_state.command.description + " is not interactable.");
    return;
  }

  if (!CALL_MEMBER_FN(target, Activate)(player, 0, 0, 1)) {
    FinishCommand(player, EventType::Failed, "The game rejected interaction with " + g_state.command.description + ".");
    return;
  }
  _MESSAGE("Walker interacted with: %s", g_state.command.description.c_str());
  FinishCommand(player, EventType::Completed, "Reached and interacted with " + g_state.command.description + ".");
}

} // namespace

bool Start(uint32_t targetFormId, Intent intent, Owner owner, std::string description) {
  if (!targetFormId || owner == Owner::None || !LookupReference(targetFormId))
    return false;
  if (g_state.hasCommand && g_state.command.targetFormId == targetFormId && g_state.command.intent == intent &&
      g_state.command.owner == owner) {
    g_state.command.description = std::move(description);
    return true;
  }

  ClearCommand(PlayerCharacter::GetSingleton());
  g_state.command = {targetFormId, intent, owner, std::move(description)};
  g_state.hasCommand = true;
  _MESSAGE("Walker started target %08X with intent %d and owner %d", targetFormId, static_cast<int>(intent),
           static_cast<int>(owner));
  return true;
}

bool CanInteract(uint32_t targetFormId) { return IsInteractable(LookupReference(targetFormId)); }

void Process() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!g_state.hasCommand)
    return;
  if (!player || StartMenu::Get() ||
      (player->pcControlFlags & (PlayerCharacter::kControlFlag_Movement | PlayerCharacter::kControlFlag_Look)) != 0) {
    Pause();
    return;
  }
  if (!EnsureWaypointMarker(player)) {
    FinishCommand(player, EventType::Failed, "Could not create a navigation waypoint.");
    return;
  }

  auto *target = LookupReference(g_state.command.targetFormId);
  if (!target) {
    FinishCommand(player, EventType::Failed, g_state.command.description + " is no longer available.");
    return;
  }
  auto *currentCell = player->GetParentCell();
  if (target != g_state.trackedTarget || currentCell != g_state.trackedCell) {
    ReleaseMovementKeys();
    ResetRouteState();
    g_state.trackedTarget = target;
    g_state.trackedCell = currentCell;
  }

  const float deltaTime = GetFrameTime();
  const float distance = Distance3D(player->GetPos(), target->GetPos());
  const bool shouldMove = distance >= kInteractionDistance;
  const PathPoint3 navigationPoint = ResolveNavigationPoint(player, target);
  if (!UpdateStuckState(player, shouldMove, deltaTime)) {
    FinishCommand(player, EventType::Failed, "Could not reach " + g_state.command.description + ".");
    return;
  }

  PathPoint3 movementPoint = navigationPoint;
  if (g_state.recovering) {
    auto *cell = GetNavigationCell(player);
    NiPoint4 closest = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    if (cell && Pathing::GetPointNavMesh(cell, g_state.waypointMarker->GetPos(), false, 0.0f, closest))
      movementPoint = {closest.x, closest.y, closest.z};
  }
  const NiPoint3 lookTarget = shouldMove ? UpdateSmoothedLookTarget(movementPoint, deltaTime) : target->GetPos();
  UpdateLook(player, lookTarget, deltaTime);
  const bool closeToTarget = distance < kCloseLookDistance;
  const float pitchError = UpdatePitch(player, target, closeToTarget, deltaTime);
  const float headingError = Math::GetHeadingBetweenPoints(
      player->pos.x, player->pos.y, Math::ToDegrees(EffectiveYaw(player)), target->pos.x, target->pos.y);
  const bool aligned = std::abs(headingError) <= kLookAlignmentTolerance && pitchError <= kLookAlignmentTolerance;
  UpdateMovement(player, movementPoint, shouldMove);
  TryComplete(player, target, distance, aligned, deltaTime);
}

void Pause() {
  auto *player = PlayerCharacter::GetSingleton();
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
}

void Stop() {
  ClearCommand(PlayerCharacter::GetSingleton());
  g_events.clear();
}

bool IsActive() { return g_state.hasCommand; }

Owner GetOwner() { return g_state.hasCommand ? g_state.command.owner : Owner::None; }

uint32_t GetTargetFormId() { return g_state.hasCommand ? g_state.command.targetFormId : 0; }

std::vector<Event> TakeEvents() {
  std::vector<Event> events;
  events.swap(g_events);
  return events;
}

} // namespace Walker
