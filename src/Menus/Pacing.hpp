#pragma once

#include "utils/ScopedState.hpp"
#include <chrono>

namespace Menus::Pacing {

using Clock = ScopedState::Clock;

// One step of a visible menu operation (a click, a page turn). Slow enough for a viewer to follow the screen.
inline constexpr auto kStepDelay = std::chrono::milliseconds(350);
// How long a chosen dialogue line or popup option is shown selected before it is clicked.
inline constexpr auto kSelectionDelay = std::chrono::milliseconds(1500);
// How long a persistently failing registration waits before it is tried (and logged) again.
inline constexpr auto kRegisterRetryDelay = std::chrono::seconds(2);
// How long a native close request may take before it is reported as failed.
inline constexpr auto kCloseTimeout = std::chrono::seconds(5);

} // namespace Menus::Pacing
