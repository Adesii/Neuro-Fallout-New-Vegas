#pragma once

#include "Actions/Action.hpp"
#include <chrono>
#include <optional>

namespace Menus::PipBoyHandler {

// Main Pip-Boy tabs share native opening/closing, pacing and execution lockout.
// Tab-specific observation and actions belong in their own handlers.
enum class Tab { Stats, Items, Data };
using Clock = std::chrono::steady_clock;
inline constexpr auto kStepDelay = std::chrono::milliseconds(350);

void Reset();
bool Process();
bool IsOpen();
bool IsActive(Tab tab);
bool IsTransitioning();
bool IsExecuting();
std::optional<Tab> CurrentTab();
Actions::PreparedAction PrepareOpen(Tab tab);
Actions::PreparedAction PrepareClose();

} // namespace Menus::PipBoyHandler
