#pragma once

#include <string>

class MessageMenu;
class Tile;

namespace Menus::MessagePopupHandler {

struct SelectionSnapshot {
  MessageMenu *owner = nullptr;
  Tile *tile = nullptr;
  int index = -1;
  std::string signature;
};

// Returns true while a popup (or its visible selection) blocks gameplay automation.
bool Process();
bool ValidateSelection(int index, SelectionSnapshot &snapshot, std::string &error);
bool RevalidateSelection(const SelectionSnapshot &snapshot, std::string &error);
void StartExecution(const SelectionSnapshot &snapshot);
void Reset();

} // namespace Menus::MessagePopupHandler
