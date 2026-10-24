#pragma once

#include "Menus/Pacing.hpp"
#include "utils/ScopedState.hpp"

class Menu;

namespace Menus {

// An accepted close of a native menu: the menu's own exit control is clicked after a visible step and the menu must
// then leave. Used by the container menu and by the stubbed menus (the same steps, different control and wording).
struct CloseRequest {
  ScopedState::StepTimer timer; // counts only while the menu is actionable (nothing covers it)
  Pacing::Clock::time_point nextStepAt{};
  bool clicked = false;
};

enum class CloseStep {
  Waiting,  // nothing to report this frame (covered by a popup, waiting for the step delay, or already clicked)
  Clicked,  // the exit control was clicked this frame
  TimedOut, // the menu did not leave within Pacing::kCloseTimeout of actionable time
  ControlUnavailable, // the exit control is missing, hidden or could not be clicked
};

// One frame of the close. `actionable` is whether the menu owns the controls (it is on top); while it is not, neither
// the clock runs nor Exit is clicked underneath a popup. `exitControl` is the control's path from the menu root.
CloseStep AdvanceNativeClose(CloseRequest &request, ::Menu *menu, const char *exitControl, bool actionable,
                             Pacing::Clock::time_point now = Pacing::Clock::now());

} // namespace Menus
