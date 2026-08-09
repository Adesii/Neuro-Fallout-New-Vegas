#include "UIUtils.hpp"
#include "GameTiles.h"
#include "GameUI.h"

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
  char path[256] = {};
  strcpy_s(path, name);
  return menu->tile->GetComponentTile(path);
}

bool ClickControl(Menu *menu, const char *name) { return UIUtils::ClickTile(menu, GetControl(menu, name)); }
} // namespace UIUtils
