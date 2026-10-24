#include "WalkerHandler.hpp"
#include "GameData.h"
#include "GameOSDepend.h"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "Movement/AimLogic.hpp"
#include "Movement/WalkerLogic.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "decoding.h"
#include "defs/Player.h"
#include "hooks/Hooks_DirectInput8Create.h"
#include "itr/PathingCommands.h"
#include "nvse/GameForms.h"
#include "nvse/GameUI.h"
#include "utils/AimUtils.hpp"
#include "utils/InputUtils.hpp"
#include "utils/WorldUtils.hpp"
#include "utils/math.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <utility>

namespace Walker {
namespace {

using WorldUtils::Distance3D;
using WorldUtils::LookupReference;

constexpr float kInteractionDistance = 100.0f;
constexpr float kInteractionDelay = 1.0f;
// Unaligned (or with the line of sight blocked) this long within reach: interact anyway and let the game accept or
// reject it. For a plain move, the same wait before failing with "not reachable from here".
constexpr float kAlignmentGiveUpDelay = 5.0f;
// The engine path search is expensive, so the next waypoint is reused between these refreshes.
constexpr float kRepathInterval = 0.25f;
constexpr float kRepathMinInterval = 0.1f; // floor for the early refresh when the cached waypoint is reached
constexpr float kWaypointReachedDistance = 60.0f;
constexpr float kDestinationMoveTolerance = 50.0f;
constexpr float kCloseLookDistance = 400.0f;
constexpr float kLookAlignmentTolerance = 0.12f;
constexpr float kLookTargetResponse = 4.0f;
// Walker's smoothed turn; combat's snap-to-target turn lives in AimUtils.cpp (kCombatTurnRate).
constexpr float kWalkTurnRate = 3.0f;
constexpr float kTurnSpeedResponse = 4.0f;
constexpr float kTurnAccelerationResponse = 7.0f;
constexpr float kTurnReversalResponse = 2.5f;
constexpr float kTurnDeadZone = 0.01f;
constexpr UINT8 kUnboundKey = 0xFF;

// Stuck detection is ignored this long after the player moves into another cell (a load or a door).
constexpr float kCellChangeGrace = 5.0f;

// Interaction line of sight: the ray is trimmed less than combat's (35/60) because the player stands within a metre.
constexpr float kInteractionRayStart = 15.0f;
constexpr float kInteractionRayEnd = 25.0f;
constexpr float kLineOfSightInterval = 0.1f;
// A plain move counts as reached without a clear ray when the target is at about the same height.
constexpr float kSameLevelHeight = 60.0f;

// Doors considered during recovery: a closed one this close and within roughly 57 degrees of the facing (Skyrim's
// walker uses 200 units too). A locked one this close and nearly dead ahead fails the walk at once.
constexpr float kDoorSearchDistance = 200.0f;
constexpr float kDoorMaxHeightDifference = 150.0f;
constexpr float kDoorSearchCone = 1.0f;
constexpr float kDoorBlockingDistance = 130.0f;
constexpr float kDoorBlockingCone = 0.5f;
constexpr float kDoorFaceTolerance = 0.15f;
constexpr float kDoorFaceTimeout = 1.0f;
constexpr float kDoorOpenWait = 0.8f;

constexpr float kJumpPulseSeconds = 0.12f;
constexpr float kSecondJumpAt = 0.6f;
constexpr float kQuarterTurn = 1.5707964f;
constexpr float kHalfTurn = 3.1415927f;

// GetOpenState(ref) is FalloutNV.exe 1.4.0.525 0x47B250 (__cdecl). Two sources agree on the address and on the result
// values None 0 / Open 1 / Opening 2 / Closed 3 / Closing 4: itr-nvse features/DoorPinchFix.cpp (MIT, revision in
// tools/reference-manifest.json) and JohnnyGuitarNVSE BGSOpenCloseForm.
constexpr uint32_t kOpenStateClosed = 3;
using GetOpenStateFn = uint32_t(__cdecl *)(TESObjectREFR *);
const auto kGetOpenState = reinterpret_cast<GetOpenStateFn>(0x47B250);

// Just enough of ExtraLock to read the locked flag: BSExtraData is 0xC bytes and `data` follows it; the data block has
// the key at 0x04 and `flags` at 0x08 with bit 1 = locked. JIP-LN-NVSE GameExtraData.h and NVSE GameExtraData.h agree
// on those offsets; only JIP documents the bit. The width of the lock level at 0x00 differs between the two, so it is
// not read. A door without an ExtraLock is not locked.
struct LockData {
  uint8_t head[8];
  uint8_t flags;
};
struct ExtraLockView {
  BSExtraData base;
  LockData *data;
};
static_assert(sizeof(BSExtraData) == 0xC, "ExtraLock::data follows the 0xC-byte BSExtraData");

struct Command {
  uint32_t targetFormId = 0;
  Intent intent = Intent::Move;
  Owner owner = Owner::None;
  std::string description;
};

// The recovery step being carried out after the player was judged stuck.
struct Recovery {
  bool active = false;
  WalkerLogic::RecoveryStep step = WalkerLogic::RecoveryStep::Sidestep;
  bool mirrored = false;
  float elapsed = 0.0f;
  int jumps = 0;
  uint32_t doorFormId = 0;
  bool doorActivated = false;
  float doorActivatedAt = 0.0f;
};

struct WalkerState {
  TESObjectREFR *waypointMarker = nullptr;
  TESObjectREFR *trackedTarget = nullptr;
  TESObjectCELL *trackedCell = nullptr;
  NiPoint3 smoothedLookTarget = {0.0f, 0.0f, 0.0f};
  NiPoint3 lastPlayerPosition = {0.0f, 0.0f, 0.0f};
  float interactionTime = 0.0f;
  float unalignedTime = 0.0f;
  float blockedTime = 0.0f;
  float navigationAge = 0.0f;
  float lineOfSightTimer = 0.0f;
  float turnVelocity = 0.0f;
  bool lookTargetInitialized = false;
  bool positionInitialized = false;
  bool alignmentGaveUp = false;
  bool navigationValid = false;
  bool lineOfSight = false;
  PathPoint3 navigationPoint{};
  NiPoint3 navigationDestination = {0.0f, 0.0f, 0.0f};
  WalkerLogic::StuckDetector stuck;
  WalkerLogic::OscillationDetector oscillation;
  WalkerLogic::ProgressTracker progress;
  WalkerLogic::RecoveryPlanner planner;
  Recovery recovery;
  std::string lockedDoor; // a locked closed door seen in front of the player while stuck
  // Per command, not per route: they carry on across a cell change.
  WalkerLogic::ProgressReminder reminder;
  WalkerLogic::PauseNotice pauseNotice;
  bool hasCommand = false;
  Command command;
  UINT8 heldMovementKeys[4] = {kUnboundKey, kUnboundKey, kUnboundKey, kUnboundKey};
};

WalkerState g_state;
std::vector<Event> g_events;
bool g_lookSuppressed = false;
InputUtils::PulseQueue g_jumpPulses{InputCore::Owner::WalkerPulse};

using AimUtils::EffectiveYaw;
using AimUtils::SetPitch;
using AimUtils::SetYaw;

bool IsInteractable(TESObjectREFR *ref) {
  if (!ref || !ref->baseForm)
    return false;
  if (ref->IsActor())
    return true;

  const UINT8 type = ref->baseForm->eFormType;
  if (type == _FormType::TESObjectACTI || type == _FormType::BGSTalkingActivator || type == _FormType::TESObjectDOOR ||
      type == _FormType::TESObjectCONT || type == _FormType::BGSTerminal || type == _FormType::TESFlora ||
      type == _FormType::TESFurniture)
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

float GetFrameTime() {
  auto *time = TimeGlobal::Get();
  return time ? std::clamp(time->secondsPassed, 0.0f, 0.1f) : 0.0f;
}

// Game time stands still in menus, so the paused-command notice runs on the wall clock.
float RealFrameDelta() {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point last{};
  const auto now = Clock::now();
  const float seconds = last == Clock::time_point{} ? 0.0f : std::chrono::duration<float>(now - last).count();
  last = now;
  return seconds > 0.5f ? 0.0f : std::clamp(seconds, 0.0f, 0.25f);
}

bool IsValidPoint(const PathPoint3 &point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

float Distance2D(const NiPoint3 &left, const NiPoint3 &right) { return std::hypot(left.x - right.x, left.y - right.y); }

void ReleaseMovementKeys() {
  auto &input = DIHookControl::GetSingleton();
  for (UINT8 &key : g_state.heldMovementKeys) {
    if (key != kUnboundKey)
      input.SetKeyHeldState(InputCore::Owner::Walker, key, false);
    key = kUnboundKey;
  }
}

int RetryBudget() {
  return g_state.command.owner == Owner::Quest ? WalkerLogic::RecoveryPlanner::kQuestBudget
                                               : WalkerLogic::RecoveryPlanner::kDefaultBudget;
}

void ResetRouteState() {
  g_jumpPulses.ReleaseAll();
  g_state.trackedTarget = nullptr;
  g_state.trackedCell = nullptr;
  g_state.interactionTime = 0.0f;
  g_state.unalignedTime = 0.0f;
  g_state.blockedTime = 0.0f;
  g_state.lineOfSightTimer = 0.0f;
  g_state.lineOfSight = false;
  g_state.alignmentGaveUp = false;
  g_state.navigationValid = false;
  g_state.navigationAge = 0.0f;
  g_state.turnVelocity = 0.0f;
  g_state.lookTargetInitialized = false;
  g_state.positionInitialized = false;
  g_state.stuck.Reset();
  g_state.oscillation.Reset();
  g_state.progress.Reset();
  g_state.planner.Reset(RetryBudget());
  g_state.recovery = {};
  g_state.lockedDoor.clear();
}

void ClearCommand(PlayerCharacter *player) {
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
  ResetRouteState();
  g_state.reminder.Reset();
  g_state.pauseNotice.Reset();
  g_state.hasCommand = false;
  g_state.command = {};
}

Event MakeEvent(EventType type) {
  Event event;
  event.type = type;
  event.owner = g_state.command.owner;
  event.targetFormId = g_state.command.targetFormId;
  event.description = g_state.command.description;
  return event;
}

void FinishCompleted(PlayerCharacter *player, std::string message) {
  Event event = MakeEvent(EventType::Completed);
  event.message = std::move(message);
  g_events.push_back(std::move(event));
  ClearCommand(player);
}

void FinishFailed(PlayerCharacter *player, WalkerLogic::FailureReason reason, std::string detail = {}) {
  Event event = MakeEvent(EventType::Failed);
  event.reason = reason;
  event.detail = std::move(detail);
  _WARNING("Walker failed %s (reason %d)", g_state.command.description.c_str(), static_cast<int>(reason));
  g_events.push_back(std::move(event));
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

// The first path node past the waypoint-reached radius, so a node the player is already standing on does not become
// the steering target (and force a repath every frame). The last node when every one is that close.
bool BuildPathPoint(PlayerCharacter *player, const NiPoint3 &destination, PathPoint3 &pathPoint) {
  g_state.waypointMarker->pos = destination;
  Pathing::PathResult path;
  if (!Pathing::BuildPath(player, g_state.waypointMarker, path) || path.nodes.empty())
    return false;
  size_t chosen = path.nodes.size() - 1;
  for (size_t index = 1; index < path.nodes.size(); ++index) {
    const PathPoint3 &node = path.nodes[index];
    if (std::hypot(node.x - player->pos.x, node.y - player->pos.y) > kWaypointReachedDistance) {
      chosen = index;
      break;
    }
  }
  pathPoint = path.nodes[chosen];
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

PathPoint3 GetNavigationPoint(PlayerCharacter *player, TESObjectREFR *target, float deltaTime) {
  const NiPoint3 destination = target->GetPos();
  const NiPoint3 playerPosition = player->GetPos();
  if (Distance3D(playerPosition, destination) < kInteractionDistance) {
    g_state.navigationValid = false;
    return {destination.x, destination.y, destination.z};
  }

  g_state.navigationAge += deltaTime;
  bool refresh = !g_state.navigationValid || g_state.navigationAge >= kRepathInterval ||
                 Distance3D(destination, g_state.navigationDestination) > kDestinationMoveTolerance;
  if (!refresh && g_state.navigationAge >= kRepathMinInterval) {
    const float dx = playerPosition.x - g_state.navigationPoint.x;
    const float dy = playerPosition.y - g_state.navigationPoint.y;
    refresh = std::sqrt(dx * dx + dy * dy) < kWaypointReachedDistance;
  }
  if (refresh) {
    g_state.navigationPoint = ResolveNavigationPoint(player, target);
    g_state.navigationDestination = destination;
    g_state.navigationAge = 0.0f;
    g_state.navigationValid = true;
  }
  return g_state.navigationPoint;
}

// --- Doors ----------------------------------------------------------------------------------------------------

std::string DoorLabel(TESObjectREFR *door) {
  const char *name = door ? door->GetFullName() : nullptr;
  return name && name[0] ? std::string("the door \"") + name + "\"" : "a door";
}

bool IsLocked(TESObjectREFR *ref) {
  auto *extra = static_cast<ExtraLockView *>(
      static_cast<void *>(ref->extraDataList.GetExtraData(static_cast<uint8_t>(_ExtraDataType::ExtraLock))));
  return extra && extra->data && (extra->data->flags & 1) != 0;
}

struct DoorAhead {
  TESObjectREFR *ref = nullptr;
  bool closed = false;
  bool locked = false;
  float distance = FLT_MAX;
  float heading = 0.0f;
};

// The nearest ordinary door in front of the player. Load doors (they carry an ExtraTeleport) are skipped: activating
// one would carry the player into another cell in the middle of a recovery.
DoorAhead FindDoorAhead(PlayerCharacter *player) {
  DoorAhead best;
  const NiPoint3 here = player->GetPos();
  const float yawDegrees = Math::ToDegrees(EffectiveYaw(player));
  WorldUtils::ForEachLoadedReference([&](TESObjectREFR *ref, TESObjectCELL *) {
    if (!ref->baseForm || ref->baseForm->eFormType != _FormType::TESObjectDOOR)
      return;
    if (ref->extraDataList.GetExtraData(_ExtraDataType::ExtraTeleport))
      return;
    const NiPoint3 there = ref->GetPos();
    const float distance = Distance2D(here, there);
    if (distance > kDoorSearchDistance || distance >= best.distance ||
        std::abs(there.z - here.z) > kDoorMaxHeightDifference)
      return;
    const float heading = Math::GetHeadingBetweenPoints(here.x, here.y, yawDegrees, there.x, there.y);
    if (std::abs(heading) > kDoorSearchCone)
      return;
    best.ref = ref;
    best.distance = distance;
    best.heading = heading;
    best.closed = kGetOpenState(ref) == kOpenStateClosed;
    best.locked = IsLocked(ref);
  });
  return best;
}

// --- Look and movement ----------------------------------------------------------------------------------------

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
  const float desiredVelocity = std::clamp(headingError * kTurnSpeedResponse, -kWalkTurnRate, kWalkTurnRate);
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

float UpdatePitch(PlayerCharacter *player, TESObjectREFR *target, bool closeToTarget, float deltaTime) {
  float desiredPitch = 0.0f;
  if (closeToTarget)
    desiredPitch = AimUtils::DesiredPitch(player, AimUtils::TargetPoint(target, AimUtils::Purpose::Look));
  const float alpha = 1.0f - std::exp(-kLookTargetResponse * deltaTime);
  const float pitch = AimUtils::CameraPitch() + (desiredPitch - AimUtils::CameraPitch()) * alpha;
  SetPitch(player, pitch);
  return std::abs(desiredPitch - pitch);
}

void HoldMovementKey(UINT8 key, size_t slot) {
  if (key == kUnboundKey)
    return;
  DIHookControl::GetSingleton().SetKeyHeldState(InputCore::Owner::Walker, key, true);
  g_state.heldMovementKeys[slot] = key;
}

// `headingOffset` turns the walking direction relative to the navigation point (a quarter turn is a sidestep, a half
// turn backs off) without touching the view, so it also works while combat owns the aim.
void UpdateMovement(PlayerCharacter *player, const PathPoint3 &navigationPoint, bool shouldMove,
                    float headingOffset = 0.0f) {
  ReleaseMovementKeys();
  Player::SetAutoMove(player, false);
  if (!shouldMove)
    return;
  auto *input = OSInputGlobals::GetSingleton();
  if (!input)
    return;

  const float heading = AimLogic::WrapAngle(Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y,
                                                                          Math::ToDegrees(EffectiveYaw(player)),
                                                                          navigationPoint.x, navigationPoint.y) +
                                            headingOffset);
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

// --- Stuck recovery -------------------------------------------------------------------------------------------

// What a frame of recovery asks of the movement code.
struct MovementPlan {
  bool move = true;
  float headingOffset = 0.0f;
  TESObjectREFR *door = nullptr; // face this door instead of the path
};

bool IsJumpBound() { return InputUtils::ResolveControl(static_cast<int>(ControlCode::Jump)) != InputUtils::kNoControl; }

void PulseJump() {
  const uint32_t macro = InputUtils::ResolveControl(static_cast<int>(ControlCode::Jump));
  if (macro != InputUtils::kNoControl)
    g_jumpPulses.Pulse(macro, kJumpPulseSeconds);
}

void EndRecovery() {
  g_state.recovery = {};
  g_state.stuck.StartGrace(0.0f); // forget the history so the verdict needs a fresh window
  g_state.oscillation.Reset();
  g_state.navigationValid = false; // the next frame paths again, from wherever the step left the player
}

// Chooses and starts a recovery step. False when the walk was failed instead (the command is gone then).
bool BeginRecovery(PlayerCharacter *player, const char *why) {
  const DoorAhead door = FindDoorAhead(player);
  if (door.ref && door.closed && door.locked) {
    g_state.lockedDoor = DoorLabel(door.ref);
    if (door.distance <= kDoorBlockingDistance && std::abs(door.heading) <= kDoorBlockingCone) {
      FinishFailed(player, WalkerLogic::FailureReason::DoorLocked, g_state.lockedDoor);
      return false;
    }
  }
  const bool doorOpenable = door.ref && door.closed && !door.locked;
  const auto choice = g_state.planner.Next(IsJumpBound(), doorOpenable);
  if (!choice) {
    if (g_state.lockedDoor.empty())
      FinishFailed(player, WalkerLogic::FailureReason::Stuck);
    else
      FinishFailed(player, WalkerLogic::FailureReason::DoorLocked, g_state.lockedDoor);
    return false;
  }

  g_state.recovery = {};
  g_state.recovery.active = true;
  g_state.recovery.step = choice->step;
  g_state.recovery.mirrored = choice->mirrored;
  g_state.recovery.doorFormId = choice->step == WalkerLogic::RecoveryStep::Door ? door.ref->GetFormID() : 0;
  g_state.oscillation.Reset();
  g_state.stuck.StartGrace(0.0f);
  _MESSAGE("Walker recovery attempt %d (%s): step %d%s", g_state.planner.Attempts(), why,
           static_cast<int>(choice->step), choice->mirrored ? " mirrored" : "");
  return true;
}

MovementPlan StepRecovery(PlayerCharacter *player, float deltaTime) {
  using WalkerLogic::RecoveryStep;
  Recovery &recovery = g_state.recovery;
  recovery.elapsed += deltaTime;
  MovementPlan plan;
  switch (recovery.step) {
  case RecoveryStep::Door: {
    plan.move = false;
    auto *door = LookupReference(recovery.doorFormId);
    if (!door) {
      EndRecovery();
      return plan;
    }
    plan.door = door;
    if (!recovery.doorActivated) {
      const float heading = Math::GetHeadingBetweenPoints(
          player->pos.x, player->pos.y, Math::ToDegrees(EffectiveYaw(player)), door->pos.x, door->pos.y);
      // Combat owns the view while it is engaged, so do not wait for a turn that will not happen.
      if (std::abs(heading) <= kDoorFaceTolerance || recovery.elapsed >= kDoorFaceTimeout || g_lookSuppressed) {
        if (kGetOpenState(door) == kOpenStateClosed && !IsLocked(door)) {
          CALL_MEMBER_FN(door, Activate)(player, 0, 0, 1);
          _MESSAGE("Walker opened %s during recovery", DoorLabel(door).c_str());
        }
        recovery.doorActivated = true;
        recovery.doorActivatedAt = recovery.elapsed;
      }
    } else if (recovery.elapsed - recovery.doorActivatedAt >= kDoorOpenWait) {
      EndRecovery();
      return plan;
    }
    break;
  }
  case RecoveryStep::Jump:
    if (recovery.jumps == 0 || (recovery.jumps == 1 && recovery.elapsed >= kSecondJumpAt)) {
      PulseJump();
      ++recovery.jumps;
    }
    break;
  case RecoveryStep::Sidestep:
    plan.headingOffset = recovery.mirrored ? -kQuarterTurn : kQuarterTurn;
    break;
  case RecoveryStep::BackOff:
    plan.headingOffset = kHalfTurn;
    break;
  }
  if (recovery.active && recovery.elapsed >= WalkerLogic::StepDuration(recovery.step))
    EndRecovery();
  return plan;
}

// --- Completion -----------------------------------------------------------------------------------------------

void UpdateLineOfSight(PlayerCharacter *player, TESObjectREFR *target, float deltaTime) {
  g_state.lineOfSightTimer -= deltaTime;
  if (g_state.lineOfSightTimer > 0.0f)
    return;
  g_state.lineOfSightTimer = kLineOfSightInterval;
  const bool clear = WorldUtils::HasLineOfSight(player, target, AimUtils::TargetPoint(target, AimUtils::Purpose::Look),
                                                {kInteractionRayStart, kInteractionRayEnd});
  if (clear != g_state.lineOfSight)
    _DMESSAGE("Walker line of sight to %s changed: %s", g_state.command.description.c_str(),
              clear ? "clear" : "blocked");
  g_state.lineOfSight = clear;
}

void TryComplete(PlayerCharacter *player, TESObjectREFR *target, float distance, bool aligned, float deltaTime) {
  if (distance >= kInteractionDistance) {
    g_state.interactionTime = 0.0f;
    g_state.unalignedTime = 0.0f;
    g_state.blockedTime = 0.0f;
    g_state.lineOfSightTimer = 0.0f;
    return;
  }
  ReleaseMovementKeys();
  UpdateLineOfSight(player, target, deltaTime);
  if (g_state.command.intent == Intent::Move) {
    // Within 100 units in 3D can still be another floor or behind a wall: reached means a clear ray or the same level.
    const bool sameLevel = std::abs(target->GetPos().z - player->GetPos().z) <= kSameLevelHeight;
    if (g_state.lineOfSight || sameLevel) {
      FinishCompleted(player, "Reached " + g_state.command.description + ".");
      return;
    }
    g_state.blockedTime += deltaTime;
    if (g_state.blockedTime >= kAlignmentGiveUpDelay)
      FinishFailed(player, WalkerLogic::FailureReason::WrongFloor);
    return;
  }

  // Interaction prefers the view on the target and a clear ray, but gives up waiting after kAlignmentGiveUpDelay and
  // lets the game accept or reject the activation.
  if (aligned && g_state.lineOfSight) {
    g_state.unalignedTime = 0.0f;
  } else {
    g_state.interactionTime = 0.0f;
    g_state.unalignedTime += deltaTime;
    if (g_state.unalignedTime < kAlignmentGiveUpDelay)
      return;
    if (!g_state.alignmentGaveUp) {
      _WARNING("Walker could not line up with %s (%s); interacting anyway", g_state.command.description.c_str(),
               g_state.lineOfSight ? "view not on target" : "line of sight blocked");
      g_state.alignmentGaveUp = true;
    }
  }
  g_state.interactionTime += deltaTime;
  if (g_state.interactionTime < kInteractionDelay)
    return;
  if (!IsInteractable(target)) {
    FinishFailed(player, WalkerLogic::FailureReason::NotInteractable);
    return;
  }

  if (!CALL_MEMBER_FN(target, Activate)(player, 0, 0, 1)) {
    FinishFailed(player, WalkerLogic::FailureReason::ActivationRejected);
    return;
  }
  _MESSAGE("Walker interacted with: %s", g_state.command.description.c_str());
  FinishCompleted(player, "Reached and interacted with " + g_state.command.description + ".");
}

// The progress reminder. Combat's approach is constant and has its own end conditions, so it never reminds.
void UpdateReminder(float deltaTime, float walked, float remaining) {
  if (g_state.command.owner == Owner::Combat)
    return;
  const auto snapshot = g_state.reminder.Update(deltaTime, walked, g_state.command.owner == Owner::Exploration);
  if (!snapshot)
    return;
  Event event = MakeEvent(EventType::Progress);
  event.walkedDistance = snapshot->walkedDistance;
  event.walkSeconds = snapshot->walkSeconds;
  event.remainingDistance = remaining;
  g_events.push_back(std::move(event));
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
  g_state.planner.Reset(RetryBudget());
  // CombatHandler closes in on melee targets constantly; announcing each approach would only be noise.
  if (owner != Owner::Combat) {
    const char *activity = owner == Owner::Exploration  ? "Exploring toward "
                           : owner == Owner::Quest      ? "Following "
                           : intent == Intent::Interact ? "Moving to interact with "
                                                        : "Moving to ";
    Event event = MakeEvent(EventType::Started);
    event.message = std::string(activity) + g_state.command.description + ".";
    g_events.push_back(std::move(event));
  }
  _MESSAGE("Walker started target %08X with intent %d and owner %d", targetFormId, static_cast<int>(intent),
           static_cast<int>(owner));
  return true;
}

bool CanInteract(uint32_t targetFormId) { return IsInteractable(LookupReference(targetFormId)); }

void Process() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!g_state.hasCommand)
    return;
  if (!player || StartMenu::Get()) {
    Pause(PauseReason::Menu);
    return;
  }
  if ((player->pcControlFlags & (PlayerCharacter::kControlFlag_Movement | PlayerCharacter::kControlFlag_Look)) != 0) {
    Pause(PauseReason::Controls);
    return;
  }
  if (!EnsureWaypointMarker(player)) {
    FinishFailed(player, WalkerLogic::FailureReason::NoWaypoint);
    return;
  }

  auto *target = LookupReference(g_state.command.targetFormId);
  if (!target) {
    FinishFailed(player, WalkerLogic::FailureReason::TargetGone);
    return;
  }
  const float deltaTime = GetFrameTime();
  g_state.pauseNotice.Update(deltaTime, false);
  g_jumpPulses.Advance(deltaTime);

  auto *currentCell = player->GetParentCell();
  if (target != g_state.trackedTarget || currentCell != g_state.trackedCell) {
    const bool changedCell = g_state.trackedCell && g_state.trackedCell != currentCell;
    ReleaseMovementKeys();
    ResetRouteState();
    g_state.trackedTarget = target;
    g_state.trackedCell = currentCell;
    if (changedCell)
      g_state.stuck.StartGrace(kCellChangeGrace); // positions jump and the world is still streaming in
  }

  const NiPoint3 playerPosition = player->GetPos();
  float walked = 0.0f;
  if (g_state.positionInitialized) {
    walked = Distance2D(playerPosition, g_state.lastPlayerPosition);
    if (walked > WalkerLogic::StuckDetector::kTeleportDistance)
      walked = 0.0f;
  }
  g_state.lastPlayerPosition = playerPosition;
  g_state.positionInitialized = true;

  const float distance = Distance3D(playerPosition, target->GetPos());
  const bool shouldMove = distance >= kInteractionDistance;
  const PathPoint3 navigationPoint = GetNavigationPoint(player, target, deltaTime);

  MovementPlan plan;
  plan.move = shouldMove;
  if (!shouldMove) {
    g_state.stuck.Update(deltaTime, playerPosition.x, playerPosition.y, false);
    g_state.oscillation.Reset();
    g_state.progress.Update(deltaTime, distance, false);
    g_state.recovery = {};
  } else if (g_state.recovery.active) {
    plan = StepRecovery(player, deltaTime);
  } else {
    g_state.planner.NoteTravel(walked);
    if (g_state.progress.Update(deltaTime, distance, true)) {
      FinishFailed(player, WalkerLogic::FailureReason::NoProgress);
      return;
    }
    const bool stuck = g_state.stuck.Update(deltaTime, playerPosition.x, playerPosition.y, true);
    const bool oscillating = g_state.oscillation.Update(deltaTime, distance);
    if (stuck || oscillating) {
      if (!BeginRecovery(player, stuck ? "no player movement" : "target distance oscillation"))
        return;
      plan = StepRecovery(player, 0.0f);
    }
  }

  float pitchError = 0.0f;
  if (!g_lookSuppressed) {
    NiPoint3 lookTarget = target->GetPos();
    TESObjectREFR *lookObject = target;
    bool closeToTarget = distance < kCloseLookDistance;
    if (plan.door) {
      lookTarget = plan.door->GetPos();
      lookObject = plan.door;
      closeToTarget = true;
    } else if (shouldMove) {
      lookTarget = UpdateSmoothedLookTarget(navigationPoint, deltaTime);
    }
    UpdateLook(player, lookTarget, deltaTime);
    pitchError = UpdatePitch(player, lookObject, closeToTarget, deltaTime);
  }
  const float headingError = Math::GetHeadingBetweenPoints(
      player->pos.x, player->pos.y, Math::ToDegrees(EffectiveYaw(player)), target->pos.x, target->pos.y);
  const bool aligned = std::abs(headingError) <= kLookAlignmentTolerance && pitchError <= kLookAlignmentTolerance;
  UpdateMovement(player, navigationPoint, plan.move, plan.headingOffset);
  UpdateReminder(deltaTime, walked, distance);
  TryComplete(player, target, distance, aligned, deltaTime);
}

void Pause(PauseReason reason) {
  auto *player = PlayerCharacter::GetSingleton();
  ReleaseMovementKeys();
  g_jumpPulses.ReleaseAll();
  Player::SetAutoMove(player, false);
  if (!g_state.hasCommand)
    return;
  if (!g_state.pauseNotice.Update(RealFrameDelta(), true))
    return;
  const char *cause = reason == PauseReason::PipBoy     ? "the Pip-Boy is being used"
                      : reason == PauseReason::Controls ? "your controls are disabled (a scripted scene or cutscene)"
                                                        : "a menu is open";
  Event event = MakeEvent(EventType::Paused);
  event.detail = cause;
  g_events.push_back(std::move(event));
}

void Stop() {
  ClearCommand(PlayerCharacter::GetSingleton());
  g_events.clear();
}

bool IsActive() { return g_state.hasCommand; }

Owner GetOwner() { return g_state.hasCommand ? g_state.command.owner : Owner::None; }

uint32_t GetTargetFormId() { return g_state.hasCommand ? g_state.command.targetFormId : 0; }

std::string GetDescription() { return g_state.hasCommand ? g_state.command.description : std::string(); }

void SetLookSuppressed(bool suppressed) { g_lookSuppressed = suppressed; }

std::vector<Event> TakeEvents() {
  std::vector<Event> events;
  events.swap(g_events);
  return events;
}

} // namespace Walker
