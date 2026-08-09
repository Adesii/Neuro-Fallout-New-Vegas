#pragma once

#include <string>

class DialogMenu;
class Tile;

namespace Menus::DialogHandler {

struct SelectionSnapshot {
  DialogMenu *owner = nullptr;
  Tile *tile = nullptr;
  int index = -1;
  std::string signature;
};

void Observe();
bool Process(bool automationAllowed);
bool ValidateSelection(int index, SelectionSnapshot &snapshot, std::string &error);
bool RevalidateSelection(const SelectionSnapshot &snapshot, std::string &error);
void StartExecution(const SelectionSnapshot &snapshot);
void Reset();

} // namespace Menus::DialogHandler
