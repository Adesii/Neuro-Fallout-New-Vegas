#include "NativeExit.hpp"
#include "GameUI.h"
#include "utils/UIUtils.hpp"

namespace Menus {

CloseStep AdvanceNativeClose(CloseRequest &request, ::Menu *menu, const char *exitControl, bool actionable,
                             Pacing::Clock::time_point now) {
  request.timer.Tick(actionable, now);
  if (!actionable)
    return CloseStep::Waiting;
  if (!request.timer.Started()) {
    request.timer.Start(now);
    request.nextStepAt = now + Pacing::kStepDelay;
  }
  if (request.timer.Expired(Pacing::kCloseTimeout))
    return CloseStep::TimedOut;
  if (request.clicked || now < request.nextStepAt)
    return CloseStep::Waiting;
  auto *control = UIUtils::GetControl(menu, exitControl);
  if (!UIUtils::ClickAvailable(menu, control))
    return CloseStep::ControlUnavailable;
  request.clicked = true;
  return CloseStep::Clicked;
}

} // namespace Menus
