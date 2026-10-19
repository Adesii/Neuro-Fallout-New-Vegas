#pragma once

#include <chrono>

class QuantityMenu;

namespace Inventory {

inline constexpr auto kQuantityStepDelay = std::chrono::milliseconds(80);

enum class QuantityStep { Adjusting, Confirmed, Mismatch, Failed };

// The active QuantityMenu, or nullptr. Callers decide whether the prompt is their own.
QuantityMenu *ActiveQuantityMenu();
void CancelQuantity(QuantityMenu &menu);
// Drives a whole-stack prompt opened by the caller's own activation through native
// arrow and OK controls. Adjusting means wait kQuantityStepDelay before the next step;
// Mismatch means the prompt's maximum is not the captured stack count.
QuantityStep AdvanceWholeStack(QuantityMenu &menu, int count);

} // namespace Inventory
