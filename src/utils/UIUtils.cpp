#include "UIUtils.hpp"
#include "GameTiles.h"
#include "GameUI.h"
#include <string>

namespace UIUtils {

std::string CopyBoundedString(const char *text, size_t capacity) {
  if (!text)
    return {};
  size_t length = 0;
  while (length < capacity && text[length] != '\0')
    ++length;
  return std::string(text, length);
}

std::string GetTileString(Tile *tile) {
  if (!tile)
    return {};
  auto *value = tile->GetValue(kTileValue_string);
  return value && value->str ? value->str : "";
}

bool IsTopMenu(Menu *menu, uint32_t menuId) {
  if (!menu)
    return false;
  if (Interface::GetTopMenuID() == menuId)
    return true;
  auto *manager = InterfaceManager::GetSingleton();
  return manager && manager->IsInMenuMode() && manager->activeMenu == menu;
}

bool ClickTile(Menu *menu, Tile *tile) {
  if (!menu || !tile)
    return false;
  auto *id = tile->GetValue(kTileValue_id);
  if (!id)
    return false;
  menu->HandleClick(id->num, tile);
  return true;
}

Tile *GetControl(Menu *menu, const char *name) {
  if (!menu || !menu->tile)
    return nullptr;
  // Tile::GetComponent writes NULs into the path while it walks it (JG GameTiles.cpp), so it needs a writable copy of
  // any length: a fixed buffer overflowed (and hit the CRT's invalid-parameter handler) on a long name.
  std::string path(name);
  return menu->tile->GetComponentTile(path.data());
}

bool ClickControl(Menu *menu, const char *name) { return UIUtils::ClickTile(menu, GetControl(menu, name)); }

bool IsControlAvailable(Tile *tile) {
  if (!tile)
    return false;
  auto *visible = tile->GetValue(kTileValue_visible);
  auto *enabled = tile->GetValue(kTileValue_target);
  return (!visible || visible->num != 0.0f) && (!enabled || enabled->num != 0.0f);
}
} // namespace UIUtils
