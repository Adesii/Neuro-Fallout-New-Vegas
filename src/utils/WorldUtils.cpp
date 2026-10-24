#include "WorldUtils.hpp"
#include "GameData.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "decoding.h"
#include "itr/internal/EngineFunctions.h"
#include "itr/internal/RayCast.h"
#include "utils/AimUtils.hpp"
#include <cmath>
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

namespace {
constexpr uint8_t kLineOfSightLayer =
    37; // LAYER_LINEOFSIGHT, the engine vision filter (see src/itr/NavmeshCommands.cpp)
} // namespace

bool RayBlocked(const NiPoint3 &from, const NiPoint3 &to) {
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
  if (!Engine::TESPickObject(&ray, true))
    return false; // no cast was performed; do not claim a blockage that was not observed
  return ray.hitFraction < 1.0f;
}

bool HasLineOfSight(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &targetPoint, LineOfSightTrim trim) {
  if (player->reticleActor && player->reticleActor == target)
    return true;
  const NiPoint3 eye = AimUtils::EyePosition(player);
  const float dx = targetPoint.x - eye.x;
  const float dy = targetPoint.y - eye.y;
  const float dz = targetPoint.z - eye.z;
  const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!(length > trim.start + trim.end))
    return true; // already within arm's reach of it
  const float start = trim.start / length;
  const float end = 1.0f - trim.end / length;
  const NiPoint3 from = {eye.x + dx * start, eye.y + dy * start, eye.z + dz * start};
  const NiPoint3 to = {eye.x + dx * end, eye.y + dy * end, eye.z + dz * end};
  return !RayBlocked(from, to);
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
