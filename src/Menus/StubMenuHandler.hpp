#pragma once

#include "Actions/Action.hpp"

// Placeholder for menus whose real automation is not written yet. While one of them is open Neuro gets a single
// close_* action and a context message saying the rest is not implemented, so she is never stuck in it. Replace an
// entry with a real handler (and delete it from the table) when that menu is implemented.
namespace Menus::StubMenuHandler {

void Reset();
// True while a stubbed menu is open (gameplay stays blocked).
bool Process();
Actions::PreparedAction PrepareClose(unsigned menuId);

} // namespace Menus::StubMenuHandler
