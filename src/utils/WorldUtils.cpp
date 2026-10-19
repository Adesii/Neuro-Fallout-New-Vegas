#include "WorldUtils.hpp"
#include "GameData.h"
#include "GameForms.h"
#include "GameObjects.h"
#include "Gamebryo/NiPoint3.hpp"
#include "decoding.h"
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
