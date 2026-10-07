#pragma once

#include "Actions/Action.hpp"
#include "Inventory/ItemQuery.hpp"

namespace Menus::InventoryHandler {

enum class Operation { Equip, Unequip, Use, Drop };

void Reset();
bool Process();
bool IsExecuting();
Actions::PreparedAction PrepareQuery(int page, Inventory::ItemQueryType type);
Actions::PreparedAction PrepareItem(Operation operation, int index);

} // namespace Menus::InventoryHandler
