#pragma once

#include <cstddef>
#include <string>

class Menu;
class Tile;

namespace UIUtils {

std::string CopyBoundedString(const char *text, size_t capacity);
std::string GetTileString(Tile *tile);
bool ClickTile(Menu *menu, Tile *tile);

} // namespace UIUtils
