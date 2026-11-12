#include "WorldUtils.hpp"
#include "GameData.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "GameProcess.h"
#include "Gamebryo/NiPoint3.hpp"
#include "decoding.h"
#include "havok.h"
#include "itr/internal/EngineFunctions.h"
#include "itr/internal/RayCast.h"
#include "utils/AimUtils.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace WorldUtils {

TESObjectREFR *LookupReference(uint32_t formId) {
  auto *form = TESForm::GetFormByNumericID(formId);
  if (!form || !form->IsReference())
    return nullptr;
  auto *ref = static_cast<TESObjectREFR *>(form);
  return ref->GetDeleted() || ref->GetDisabled() ? nullptr : ref;
}

float Distance3D(const NiPoint3 &left, const NiPoint3 &right) {
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  const float z = left.z - right.z;
  return std::sqrt(x * x + y * y + z * z);
}

Objects::Classification ClassifyObject(TESObjectREFR *ref) {
  using Objects::Category;
  if (!ref || !ref->baseForm)
    return {};
  if (ref->IsActor())
    return {static_cast<Actor *>(ref)->GetDead() ? Category::Corpse : Category::Actor, true};

  Category category = Category::None;
  switch (ref->baseForm->eFormType) {
  case _FormType::TESObjectACTI:
  case _FormType::BGSTalkingActivator:
    category = Category::Activator;
    break;
  case _FormType::TESObjectCONT:
    category = Category::Container;
    break;
  case _FormType::TESObjectDOOR:
    category = Category::Door;
    break;
  case _FormType::BGSTerminal:
    category = Category::Terminal;
    break;
  case _FormType::TESFlora:
    category = Category::Harvestable;
    break;
  case _FormType::TESFurniture:
    category = Category::Furniture;
    break;
  case _FormType::TESObjectWEAP:
    category = Category::Weapon;
    break;
  case _FormType::TESObjectARMO:
  case _FormType::TESObjectCLOT:
    category = Category::Armor;
    break;
  case _FormType::TESAmmo:
    category = Category::Ammo;
    break;
  case _FormType::AlchemyItem:
  case _FormType::IngredientItem:
    category = Category::Aid;
    break;
  case _FormType::TESObjectBOOK:
  case _FormType::BGSNote:
    category = Category::Readable;
    break;
  case _FormType::TESKey:
  case _FormType::TESObjectMISC:
    category = Category::Loot;
    break;
  default:
    return {};
  }
  if (Objects::IsLooseItem(category) && ref->extraDataList.GetExtraData(_ExtraDataType::ExtraPrimitive))
    return {category, false};
  return {category, true};
}

namespace {
constexpr uint8_t kLineOfSightLayer = Layer::kLineOfSight; // see src/itr/NavmeshCommands.cpp
} // namespace

const char *LayerName(uint8_t layer) {
  static constexpr const char *kNames[Layer::kCount] = {
      "NONE",
      "STATIC",
      "ANIM_STATIC",
      "TRANSPARENT",
      "CLUTTER",
      "WEAPON",
      "PROJECTILE",
      "SPELL",
      "BIPED",
      "TREES",
      "PROPS",
      "WATER",
      "TRIGGER",
      "TERRAIN",
      "TRAP",
      "NONCOLLIDABLE",
      "CLOUD_TRAP",
      "GROUND",
      "PORTAL",
      "DEBRIS_SMALL",
      "DEBRIS_LARGE",
      "ACOUSTIC_SPACE",
      "ACTORZONE",
      "PROJECTILEZONE",
      "GASTRAP",
      "SHELLCASING",
      "TRANSPARENT_SMALL",
      "INVISIBLE_WALL",
      "TRANSPARENT_SMALL_ANIM",
      "DEADBIP",
      "CHARCONTROLLER",
      "AVOIDBOX",
      "COLLISIONBOX",
      "CAMERASPHERE",
      "DOORDETECTION",
      "CAMERAPICK",
      "ITEMPICK",
      "LINEOFSIGHT",
      "PATHPICK",
      "CUSTOMPICK1",
      "CUSTOMPICK2",
      "SPELLEXPLOSION",
      "DROPPINGPICK",
  };
  return layer < Layer::kCount ? kNames[layer] : "?";
}

// The Havok collision group of the player's character controller, so a ray can carry it and pass through the player's
// own body. Without it the ray can hit the player: crouched with a weapon out, the line-of-sight ray hit something
// 62 units in front of the camera at about 1 m (designer's log, 2026-11-08), which is where the arms and the gun are.
// JIP-LN-NVSE does the same for its view rays (`internal/jip_core.cpp`, _GetRayCastObject: the player's filter info
// with the layer replaced). Every link is named in the vendored JG definitions: Actor::baseProcess (0x68),
// MiddleHighProcess::charCtrl (0x138), bhkCharacterController::chrPhantom (0x594), bhkRefObject::refObject (0x08), and
// hkpWorldObject's filter info at 0x2C (layer, flags, then the 16-bit group at 0x2E, which JIP names `group`).
// 0 when any link is missing; a ray with group 0 behaves as before.
uint16_t PlayerCollisionGroup(PlayerCharacter *player) {
  auto *process = player ? player->baseProcess : nullptr;
  if (!process || process->processLevel > 1) // charCtrl exists from MiddleHighProcess (level 1) up
    return 0;
  auto *controller = static_cast<MiddleHighProcess *>(process)->charCtrl;
  auto *phantom = controller ? controller->chrPhantom : nullptr;
  auto *object = phantom ? static_cast<hkpWorldObject *>(phantom->refObject) : nullptr;
  if (!object)
    return 0;
  uint16_t group = 0;
  std::memcpy(&group, object->unk2E, sizeof(group));
  return group;
}

// Exterior cells are 4096 units square and keyed by their coordinates in the worldspace's cell map, as
// src/itr/NavmeshCommands.cpp looks them up for cover points.
TESObjectCELL *CellAt(PlayerCharacter *player, float x, float y) {
  auto *tes = TES::GetSingleton();
  if (tes && tes->currentInterior)
    return tes->currentInterior;
  TESObjectCELL *own = player ? player->GetParentCell() : nullptr;
  TESWorldSpace *world = own ? own->worldSpace : nullptr;
  if (!world || !world->pCellMap)
    return own;
  const INT32 cellX = static_cast<INT32>(std::floor(x / 4096.0f));
  const INT32 cellY = static_cast<INT32>(std::floor(y / 4096.0f));
  const INT32 key = ((cellX & 0xFFFF) << 16) | (cellY & 0xFFFF);
  TESObjectCELL *cell = nullptr;
  world->pCellMap->GetAt(key, cell);
  return cell ? cell : own;
}

// bhkCharacterController::shapeHeight (0x5F4) and shapeRadius (0x5F8), in Havok units. JIP-LN-NVSE names them
// (`internal/havok.h`); the vendored JG definition has unnamed words there, so they are read by offset. Read in game
// on 2026-11-10 (NWalkerProbe, Primm): radius 2.8932 and height 18.2880, which is 20.3 and exactly 128.0 game units,
// the standard actor height. That is what backs this second-hand offset.
bool PlayerCollisionShape(PlayerCharacter *player, float &radius, float &height) {
  auto *process = player ? player->baseProcess : nullptr;
  if (!process || process->processLevel > 1)
    return false;
  auto *controller = static_cast<MiddleHighProcess *>(process)->charCtrl;
  if (!controller)
    return false;
  float havokHeight = 0.0f;
  float havokRadius = 0.0f;
  std::memcpy(&havokHeight, reinterpret_cast<const uint8_t *>(controller) + 0x5F4, sizeof(havokHeight));
  std::memcpy(&havokRadius, reinterpret_cast<const uint8_t *>(controller) + 0x5F8, sizeof(havokRadius));
  radius = havokRadius / kHavokScale;
  height = havokHeight / kHavokScale;
  return std::isfinite(radius) && std::isfinite(height) && radius > 5.0f && radius < 100.0f && height > radius &&
         height < 400.0f;
}

// What the ray payload holds after TES::Pick, each from the sources named:
// - the hit fraction at 0x40 and the root collidable of what was hit at 0x80: itr-nvse `internal/RayCast.h` and
//   JIP-LN-NVSE `internal/jip_core.cpp` (RayCastData: flt40, cdBody) agree;
// - the surface normal at 0x30: itr-nvse only (its OnFootContactHandler.cpp reads it for footstep contacts), so it
//   is checked for being a unit vector and dropped otherwise;
// - the collidable's filter info at +0x1C, whose low 7 bits are the layer: itr-nvse `internal/HavokLayout.h`
//   (HkpCollidableView, used as `filterInfo & 0x7F`) and JIP-LN-NVSE `internal/havok.h` (hkpWorldObject: cdBody at
//   0x10, layerType at 0x2C) agree;
// - the returned NiAVObject and the reference it belongs to: see Engine::TESPickObjectHit and FindReferenceFor3D.
RayDetail CastRay(const NiPoint3 &from, const NiPoint3 &to, uint8_t layer, uint16_t ignoreGroup, bool findReference) {
  RayDetail detail;
  RayCastData ray = {};
  ray.pos0[0] = from.x * kHavokScale;
  ray.pos0[1] = from.y * kHavokScale;
  ray.pos0[2] = from.z * kHavokScale;
  ray.pos1[0] = to.x * kHavokScale;
  ray.pos1[1] = to.y * kHavokScale;
  ray.pos1[2] = to.z * kHavokScale;
  ray.hitFraction = 1.0f;
  ray.unk44[0] = 0xFFFFFFFF;
  ray.unk44[6] = 0xFFFFFFFF;
  ray.layerType = layer;
  ray.group = ignoreGroup;
  if (!GetTES())
    return detail;
  void *object = Engine::TESPickObjectHit(&ray, true);
  detail.performed = true;
  detail.budgetSpent = ray.budgetSpent != 0;
  if (!std::isfinite(ray.hitFraction) || ray.hitFraction < 0.0f || ray.hitFraction >= 1.0f)
    return detail;
  detail.hit = true;
  detail.fraction = ray.hitFraction;
  detail.point = {from.x + (to.x - from.x) * detail.fraction, from.y + (to.y - from.y) * detail.fraction,
                  from.z + (to.z - from.z) * detail.fraction};
  const float length =
      std::sqrt(ray.normal[0] * ray.normal[0] + ray.normal[1] * ray.normal[1] + ray.normal[2] * ray.normal[2]);
  if (std::isfinite(length) && length > 0.9f && length < 1.1f)
    detail.normal = {ray.normal[0] / length, ray.normal[1] / length, ray.normal[2] / length};
  if (ray.cdBody) {
    uint32_t filterInfo = 0;
    std::memcpy(&filterInfo, static_cast<const uint8_t *>(ray.cdBody) + 0x1C, sizeof(filterInfo));
    detail.layer = static_cast<uint8_t>(filterInfo & 0x7F);
  }
  if (findReference && object)
    detail.ref = Engine::FindReferenceFor3D(object);
  return detail;
}

namespace {

// The fraction along from -> to where the ray hit something, 1 when it is clear or no cast was performed. `group` is a
// collision group the ray ignores (PlayerCollisionGroup), 0 for none.
float RayHitFraction(const NiPoint3 &from, const NiPoint3 &to, uint16_t group = 0) {
  RayCastData ray = {};
  ray.pos0[0] = from.x * kHavokScale;
  ray.pos0[1] = from.y * kHavokScale;
  ray.pos0[2] = from.z * kHavokScale;
  ray.pos1[0] = to.x * kHavokScale;
  ray.pos1[1] = to.y * kHavokScale;
  ray.pos1[2] = to.z * kHavokScale;
  ray.hitFraction = 1.0f;
  ray.unk44[0] = 0xFFFFFFFF;
  ray.unk44[6] = 0xFFFFFFFF;
  ray.layerType = kLineOfSightLayer;
  ray.group = group;
  if (!Engine::TESPickObject(&ray, true))
    return 1.0f; // no cast was performed; do not claim a blockage that was not observed
  return std::isfinite(ray.hitFraction) ? std::clamp(ray.hitFraction, 0.0f, 1.0f) : 1.0f;
}

float Distance(const NiPoint3 &a, const NiPoint3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

bool RayBlocked(const NiPoint3 &from, const NiPoint3 &to) { return RayHitFraction(from, to) < 1.0f; }

LineOfSightTrace TraceLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint,
                                  LineOfSightTrim trim) {
  LineOfSightTrace trace;
  if (player->reticleActor && player->reticleActor == target) {
    trace.clear = trace.reticle = true;
    return trace;
  }
  trace.hasCamera = AimUtils::CameraPosition(player, trace.camera);
  if (!trace.hasCamera)
    return trace; // no usable camera: do not claim a clear view that was never checked
  const NiPoint3 &eye = trace.camera;
  const float dx = targetPoint.x - eye.x;
  const float dy = targetPoint.y - eye.y;
  const float dz = targetPoint.z - eye.z;
  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!(length > trim.start + trim.end)) {
    trace.clear = trace.tooShort = true; // already within arm's reach of it
    return trace;
  }
  const float start = trim.start / length;
  const float end = 1.0f - trim.end / length;
  trace.from = {eye.x + dx * start, eye.y + dy * start, eye.z + dz * start};
  trace.to = {eye.x + dx * end, eye.y + dy * end, eye.z + dz * end};
  trace.group = PlayerCollisionGroup(player);
  trace.hitFraction = RayHitFraction(trace.from, trace.to, trace.group);
  trace.clear = trace.hitFraction >= 1.0f;
  return trace;
}

bool HasLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint, LineOfSightTrim trim) {
  return TraceLineOfSight(player, target, targetPoint, trim).clear;
}

void LogLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint,
                    const LineOfSightTrace &trace, const char *context) {
  const NiPoint3 feet = player->GetPos();
  NiPoint3 field;
  const bool hasField = AimUtils::PlayerCameraField(player, field);
  const NiPoint3 hit = {trace.from.x + (trace.to.x - trace.from.x) * trace.hitFraction,
                        trace.from.y + (trace.to.y - trace.from.y) * trace.hitFraction,
                        trace.from.z + (trace.to.z - trace.from.z) * trace.hitFraction};
  _WARNING(
      "Line of sight %s to %08X (%s): %s; camera (%.1f %.1f %.1f) %.1f above the feet, player cameraPos field "
      "(%.1f %.1f %.1f)%s, feet (%.1f %.1f %.1f) crouching %d; target point (%.1f %.1f %.1f), %.1f from the camera; "
      "ray (ignoring player collision group %u) blocked at fraction %.3f, %.1f from the camera and %.1f short of the "
      "target point (hit z %.1f)",
      trace.clear ? "clear" : "BLOCKED", target ? target->GetFormID() : 0, context,
      !trace.hasCamera ? "no usable camera"
      : trace.reticle  ? "reticle"
      : trace.tooShort ? "within trims"
                       : "ray",
      trace.camera.x, trace.camera.y, trace.camera.z, trace.camera.z - feet.z, field.x, field.y, field.z,
      hasField ? "" : " unusable", feet.x, feet.y, feet.z, AimUtils::IsCrouching(player) ? 1 : 0, targetPoint.x,
      targetPoint.y, targetPoint.z, Distance(trace.camera, targetPoint), static_cast<unsigned>(trace.group),
      trace.hitFraction, Distance(trace.camera, hit), Distance(hit, targetPoint), hit.z);
}

void ForEachLoadedReference(const std::function<void(TESObjectREFR *, TESObjectCELL *)> &visit) {
  auto *player = PlayerCharacter::GetSingleton();
  auto *tes = TES::GetSingleton();
  if (!player || !tes)
    return;

  std::unordered_set<uint32_t> seen;
  auto scanCell = [&](TESObjectCELL *cell) {
    if (!cell)
      return;
    for (auto *node = cell->objectList.GetHead(); node; node = node->GetNext()) {
      auto *ref = node->GetItem();
      if (!ref || ref->GetDeleted() || ref->GetDisabled() || !ref->baseForm || !seen.insert(ref->GetFormID()).second)
        continue;
      visit(ref, cell);
    }
  };

  if (tes->currentInterior) {
    scanCell(tes->currentInterior);
  } else if (tes->gridCellArray && tes->gridCellArray->pGridCells) {
    const int dimension = tes->gridCellArray->iDimension;
    for (int y = 0; y < dimension; ++y) {
      for (int x = 0; x < dimension; ++x) {
        auto *gridCell = tes->gridCellArray->GetCell(x, y);
        if (gridCell)
          scanCell(gridCell->pCell);
      }
    }
  } else {
    scanCell(player->GetParentCell());
  }
}

} // namespace WorldUtils
