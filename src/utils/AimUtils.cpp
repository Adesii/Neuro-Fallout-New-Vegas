#include "AimUtils.hpp"
#include "Bethesda/BSUtilities.hpp"
#include "Bethesda/TESMain.hpp"
#include "GameForms.h"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "Movement/StanceLogic.hpp"
#include "utils/math.h"
#include <algorithm>
#include <cmath>

namespace AimUtils {
namespace {

using SetPitchWithClamp = void(__thiscall *)(Actor *, float);
using AddDeltaYaw = void(__thiscall *)(Actor *, float);
using GetEffectiveYaw = float(__thiscall *)(PlayerCharacter *, int);
const auto kSetPitchWithClamp = reinterpret_cast<SetPitchWithClamp>(0x931D90);
const auto kAddDeltaYaw = reinterpret_cast<AddDeltaYaw>(0x931D30);
const auto kGetEffectiveYaw = reinterpret_cast<GetEffectiveYaw>(0x953F20);
auto *const kCameraPitch = reinterpret_cast<float *>(0x11E0764);
auto *const kCameraYaw = reinterpret_cast<float *>(0x11E076C);

constexpr float kPitchLimit = 1.5533f;
// Combat's snap-to-target turn. Walker has its own, slower, smoothed turn (kWalkTurnRate in WalkerHandler.cpp).
constexpr float kCombatTurnResponse = 10.0f;
constexpr float kCombatTurnRate = 7.0f;
constexpr float kPitchResponse = 12.0f;

using AimLogic::WrapAngle;

AimLogic::TargetKind ClassifyTarget(TESObjectREFR *ref) {
  if (ref->IsActor())
    return ref->baseForm && ref->baseForm->eFormType == _FormType::TESCreature ? AimLogic::TargetKind::Creature
                                                                               : AimLogic::TargetKind::Character;
  if (!ref->baseForm)
    return AimLogic::TargetKind::Object;
  switch (ref->baseForm->eFormType) {
  case _FormType::TESObjectACTI:
  case _FormType::BGSTalkingActivator:
  case _FormType::TESObjectDOOR:
  case _FormType::TESObjectCONT:
  case _FormType::BGSTerminal:
  case _FormType::TESFurniture:
    return AimLogic::TargetKind::Activator;
  case _FormType::TESFlora:
    return AimLogic::TargetKind::Flora;
  default:
    return AimLogic::TargetKind::Object;
  }
}

} // namespace

float EffectiveYaw(PlayerCharacter *player) { return kGetEffectiveYaw(player, 0); }

void SetYaw(PlayerCharacter *player, float yaw) {
  yaw = WrapAngle(yaw);
  const float delta = WrapAngle(yaw - EffectiveYaw(player));
  kAddDeltaYaw(player, delta);
  player->rot.z = yaw;
  *kCameraYaw = yaw;
}

float CameraPitch() { return *kCameraPitch; }

void SetPitch(PlayerCharacter *player, float pitch) {
  if (!player)
    return;
  pitch = std::clamp(pitch, -kPitchLimit, kPitchLimit);
  kSetPitchWithClamp(player, pitch);
  player->rot.x = pitch;
  *kCameraPitch = pitch;
}

bool IsCrouching(PlayerCharacter *player) {
  return player && player->actorMover && Stance::IsSneakingFlags(player->GetMovementFlags());
}

// The rendered world camera: TESMain::GetWorldRootCamera() (JG `internal/Game/Bethesda/TESMain.cpp`: the world scene
// graph at 0x11DEB7C, its spCamera) and its world translate, as JG's LandRemapping.cpp reads it. JIP-LN-NVSE keeps the
// same object as g_mainCamera (`internal/patches_game.h`, sceneGraph->camera) and casts most of its view rays from
// g_mainCamera->WorldTranslate(). It replaced the modelled eye (feet + 100, a guess) on 2026-11-07 and the player's
// cameraPos field on 2026-11-08. Not confirmed at runtime yet; the Aim and line-of-sight log lines print both.
bool CameraPosition(PlayerCharacter *player, NiPoint3 &camera) {
  if (!player)
    return false;
  NiCamera *sceneCamera = TESMain::GetWorldSceneGraph() ? TESMain::GetWorldRootCamera() : nullptr;
  if (!sceneCamera)
    return false;
  const auto &translate = sceneCamera->m_kWorld.m_kTranslate;
  if (!AimLogic::UsableCamera(translate.x, translate.y, translate.z))
    return false;
  camera = {translate.x, translate.y, translate.z};
  return true;
}

// PlayerCharacter::cameraPos at 0xDE0: JG `nvse/GameObjects.h` and JIP-LN-NVSE `nvse/GameObjects.h` agree; JIP casts
// its crosshair pick from it (`functions_jip/jip_fn_miscellaneous.h`).
bool PlayerCameraField(PlayerCharacter *player, NiPoint3 &camera) {
  if (!player || !AimLogic::UsableCamera(player->cameraPos.x, player->cameraPos.y, player->cameraPos.z))
    return false;
  camera = {player->cameraPos.x, player->cameraPos.y, player->cameraPos.z};
  return true;
}

// The loaded root node's world bound (TESObjectREFR::Get3DSimple -> renderState->rootNode, NiAVObject::GetWorldBound
// with NiBound{kCenter, fRadius}: JG `nvse/GameObjects.h`, `internal/netimmerse.h`, `nvse/NiTypes.h`; JohnnyGuitarNVSE
// reads bounds the same way in `JG/functions/fn_gamebryo.cpp`).
bool ObjectBoundCentre(TESObjectREFR *ref, NiPoint3 &centre, float &radius) {
  auto *node = ref ? ref->Get3DSimple() : nullptr;
  if (!node || !node->m_pWorldBound)
    return false;
  const NiBound &bound = node->GetWorldBound();
  const NiPoint3 origin = ref->GetPos();
  const NiPoint3 candidate = {bound.kCenter.x, bound.kCenter.y, bound.kCenter.z};
  const float dx = candidate.x - origin.x;
  const float dy = candidate.y - origin.y;
  const float dz = candidate.z - origin.z;
  if (!AimLogic::UsableObjectBound(bound.fRadius, std::sqrt(dx * dx + dy * dy + dz * dz)))
    return false;
  centre = candidate;
  radius = bound.fRadius;
  return true;
}

namespace {

// The world position of the node called `name` under `root`, when it is a usable point on `ref`.
// BSUtilities::GetObjectByName(scene, name) is FalloutNV.exe 1.4.0.525 0x4AAE30 (JG `BSUtilities.cpp`; itr-nvse
// `DialogueCameraHandler.cpp` reads head positions through the same address). In the exe it forwards to 0xC4B310,
// which returns null for a null scene or name.
bool NodePosition(TESObjectREFR *ref, NiNode *root, const char *name, NiPoint3 &point) {
  if (!root || !name || !name[0])
    return false;
  NiAVObject *node = BSUtilities::GetObjectByName(root, name);
  if (!node)
    return false;
  const auto &translate = node->m_kWorld.m_kTranslate;
  const NiPoint3 origin = ref->GetPos();
  if (!AimLogic::UsableBodyPoint(translate.x - origin.x, translate.y - origin.y, translate.z - origin.z,
                                 ref->GetScale()))
    return false;
  point = {translate.x, translate.y, translate.z};
  return true;
}

} // namespace

// Where the game itself puts a torso shot. Every actor base has body part data (BGSBodyPartData, JG `GameForms.h`),
// indexed by part type with the torso at 0, and each part names two skeleton nodes: `VATSTarget`, the node VATS aims
// at, and `partNode`. JIP-LN-NVSE's GetBodyPartVATSTarget reads them the same way (`functions_jip/jip_fn_actor.h`:
// GetActorBase()->GetBodyPartData()->bodyParts[part]->VATSTarget).
// Checked in FalloutNV.exe 1.4.0.525: TESActorBase::GetBodyPartData is vtable slot 0x60; TESCreature's (0x5FA2E0)
// returns its own data at +0x144 or the default record, TESNPC's (0x5F0F80) the player's record for the player's base
// and the default record for everyone else.
// Checked in FalloutNV.esm (all 49 BPTD records): the torso's VATS target is a chest or upper spine node for nearly
// every body: "Bip01 Spine2" in DefaultBodyPartData (every human and ghoul), NVGecko, dogs, mole rats, deathclaws;
// "Bip01 Spine1" for super mutants, brahmin and night stalkers; "Bip01 Spine" for ants, radscorpions, robots.
// Not run in game: that the node's world transform is current for an actor that is far away or off screen.
bool ActorTorsoPoint(TESObjectREFR *ref, NiPoint3 &point, PointSource &source) {
  if (!ref || !ref->IsActor())
    return false;
  NiNode *root = ref->Get3DSimple();
  if (!root)
    return false;
  TESActorBase *base = static_cast<Actor *>(ref)->GetActorBase();
  // Only the two actor base types have the virtual; anything else here would be a call through the wrong vtable.
  if (!base || (base->eFormType != _FormType::TESNPC && base->eFormType != _FormType::TESCreature))
    return false;
  BGSBodyPartData *data = base->GetBodyPartData();
  BGSBodyPart *torso = data ? data->bodyParts[BGSBodyPartData::eBodyPart_Torso] : nullptr;
  if (!torso)
    return false;
  if (NodePosition(ref, root, torso->VATSTarget.pString, point)) {
    source = PointSource::TorsoTarget;
    return true;
  }
  if (NodePosition(ref, root, torso->partNode.pString, point)) {
    source = PointSource::TorsoNode;
    return true;
  }
  return false;
}

NiPoint3 TargetPoint(TESObjectREFR *ref, Purpose purpose, PointSource *source) {
  auto from = [source](PointSource value) {
    if (source)
      *source = value;
  };
  const AimLogic::TargetKind kind = ClassifyTarget(ref);
  if (purpose == Purpose::Shoot && ref->IsActor()) {
    NiPoint3 point;
    PointSource torso = PointSource::Origin;
    if (ActorTorsoPoint(ref, point, torso)) {
      from(torso);
      return point;
    }
    float radius = 0.0f;
    if (ObjectBoundCentre(ref, point, radius)) {
      from(PointSource::MeshBound);
      return point;
    }
    // No guessed height (designer, 2026-11-07: a real value or nothing). An actor with no usable node and no usable
    // bound has no loaded body to hit; the Aim log line names this source so it gets reported.
    from(PointSource::Origin);
    return ref->GetPos();
  }
  if (purpose == Purpose::Shoot && kind == AimLogic::TargetKind::Object) {
    NiPoint3 centre;
    float radius = 0.0f;
    if (ObjectBoundCentre(ref, centre, radius)) {
      from(PointSource::ObjectBound);
      return centre;
    }
  }
  from(PointSource::HeightTable);
  NiPoint3 point = ref->GetPos();
  point.z += AimLogic::ScaledTargetHeight(kind, purpose, ref->GetScale());
  return point;
}

float DesiredPitch(PlayerCharacter *player, const NiPoint3 &point) {
  NiPoint3 camera;
  if (!CameraPosition(player, camera))
    return CameraPitch();
  return AimLogic::PitchTo(point.x - camera.x, point.y - camera.y, point.z - camera.z);
}

Error AimAt(PlayerCharacter *player, const NiPoint3 &point, float deltaTime) {
  Error error;
  if (!player)
    return error;

  const float currentYaw = EffectiveYaw(player);
  float heading =
      Math::GetHeadingBetweenPoints(player->pos.x, player->pos.y, Math::ToDegrees(currentYaw), point.x, point.y);
  if (deltaTime > 0.0f) {
    float turn = std::clamp(heading * kCombatTurnResponse, -kCombatTurnRate, kCombatTurnRate) * deltaTime;
    if (std::abs(turn) > std::abs(heading))
      turn = heading;
    SetYaw(player, currentYaw + turn);
    heading -= turn;
  }
  error.heading = heading;

  const float desiredPitch = DesiredPitch(player, point);
  if (deltaTime > 0.0f) {
    const float alpha = 1.0f - std::exp(-kPitchResponse * deltaTime);
    SetPitch(player, CameraPitch() + (desiredPitch - CameraPitch()) * alpha);
  }
  error.pitch = desiredPitch - CameraPitch();
  return error;
}

} // namespace AimUtils
