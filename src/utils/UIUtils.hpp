#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class Menu;
class Tile;

namespace UIUtils {

std::string CopyBoundedString(const char *text, size_t capacity);
std::string GetTileString(Tile *tile);
// True when the menu is the top menu or the active menu in menu mode. Never infer this from the
// menu singleton alone: engine singletons stay allocated after their menu closes.
bool IsTopMenu(Menu *menu, uint32_t menuId);
bool ClickTile(Menu *menu, Tile *tile);
Tile *GetControl(Menu *menu, const char *name);
bool ClickControl(Menu *menu, const char *name);
// Visible and targetable, treating absent traits as their defaults.
bool IsControlAvailable(Tile *tile);
} // namespace UIUtils
