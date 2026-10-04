#pragma once

#include "Actions/Action.hpp"
#include "Inventory/ItemQuery.hpp"

namespace Menus::ContainerHandler {

void Reset();
bool Process();
Actions::PreparedAction PrepareQuery(bool ownInventory, int page, Inventory::ItemQueryType type);
Actions::PreparedAction PrepareTransfer(bool stow, int index);
Actions::PreparedAction PrepareLootAll();

} // namespace Menus::ContainerHandler
