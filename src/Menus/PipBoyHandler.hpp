#pragma once

#include "Actions/Action.hpp"
#include "Menus/Pacing.hpp"
#include <chrono>
#include <optional>

namespace Menus::PipBoyHandler {

// Main Pip-Boy tabs share native opening/closing, pacing and execution lockout.
// Tab-specific observation and actions belong in their own handlers.
enum class Tab { Stats, Items, Data };
using Pacing::Clock;
using Pacing::kStepDelay;

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
