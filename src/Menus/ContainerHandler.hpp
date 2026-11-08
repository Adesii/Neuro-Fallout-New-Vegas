#pragma once

#include "Actions/Action.hpp"
#include "Inventory/ItemQuery.hpp"

namespace Menus::ContainerHandler {

void Reset();
bool Process();
// The container is the top menu, its listing went out, and nothing is queued, retried or closing: the game is waiting
// on Neuro's next container action. The idle poker does not count such a menu as busy (todo 40).
bool AwaitsDecision();
Actions::PreparedAction PrepareQuery(bool ownInventory, int page, Inventory::ItemQueryType type);
Actions::PreparedAction PrepareTransfer(bool stow, int index);
Actions::PreparedAction PrepareLootAll();
Actions::PreparedAction PrepareClose();

} // namespace Menus::ContainerHandler
