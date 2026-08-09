#pragma once

#include <array>
#include <string>

namespace Menus::SpecialAllocationHandler {

using SpecialValues = std::array<int, 7>;

bool Process(bool unobstructed);
bool IsExecuting();
bool ValidateAllocation(const SpecialValues &values, std::string &error);
bool RevalidateAllocation(const SpecialValues &values, const void *expectedOwner, int expectedPage, int expectedTotal,
                          std::string &error);
void StartExecution(const SpecialValues &values);
void Reset();

} // namespace Menus::SpecialAllocationHandler
