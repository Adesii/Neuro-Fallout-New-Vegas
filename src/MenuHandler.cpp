#include "MenuHandler.hpp"
#include "GameUI.h"

namespace MenuHandler {
void Process() {
  auto *interfaces = InterfaceManager::GetSingleton();
  if (!interfaces)
    return;

  auto *menu = interfaces->activeMenu;
  if (!menu || !menu->tile)
    return;

  static uint32_t lastMenuID = Interface::NoMenu;
  if (menu->id == lastMenuID)
    return;

  lastMenuID = menu->id;
  _MESSAGE("Active menu ID: %u", menu->id);
}
} // namespace MenuHandler
