#include "Inventory/QuantityPrompt.hpp"
#include "GameUI.h"
#include "utils/UIUtils.hpp"
#include <algorithm>

namespace Inventory {
namespace {

// A whole stack reaches its maximum in at most this many visible steps, keeping
// large ammunition stacks well inside the executors' timeouts.
constexpr int kMaxAdjustSteps = 30;

} // namespace

QuantityMenu *ActiveQuantityMenu() {
  auto *manager = InterfaceManager::GetSingleton();
  auto *menu = manager ? manager->activeMenu : nullptr;
  return menu && menu->GetID() == Interface::Quantity ? static_cast<QuantityMenu *>(menu) : nullptr;
}

void CancelQuantity(QuantityMenu &menu) { UIUtils::ClickControl(&menu, "NOGLOW_BRANCH/QM_MainRect/QM_CancelButton"); }

QuantityStep AdvanceWholeStack(QuantityMenu &menu, int count) {
  auto *maximum = menu.tile28 ? menu.tile28->GetValue(kTileValue_user0) : nullptr;
  if (!maximum || maximum->num != static_cast<float>(count))
    return QuantityStep::Mismatch;
  if (menu.currentQtt < maximum->num) {
    // Several native arrow clicks per step; the amount is re-read after each click.
    const int clicks = std::max(1, (count + kMaxAdjustSteps - 1) / kMaxAdjustSteps);
    for (int click = 0; click < clicks && menu.currentQtt < maximum->num; ++click)
      if (!UIUtils::ClickControl(&menu, "NOGLOW_BRANCH/QM_MainRect/QM_IncreaseArrow"))
        return QuantityStep::Failed;
    return QuantityStep::Adjusting;
  }
  if (menu.currentQtt != maximum->num || !UIUtils::ClickControl(&menu, "NOGLOW_BRANCH/QM_MainRect/QM_OKButton"))
    return QuantityStep::Failed;
  return QuantityStep::Confirmed;
}

} // namespace Inventory
