#pragma once

#include <string>

class Tile;

namespace Menus::TraitsHandler {

struct SelectionSnapshot {
  void *owner = nullptr;
  Tile *selectTile = nullptr;
  Tile *unselectTile = nullptr;
  int selectIndex = -1;
  int unselectIndex = -1;
  bool shouldUnselect = false;
  std::string signature;
};

struct DoneSnapshot {
  void *owner = nullptr;
  std::string signature;
};

bool Process(bool unobstructed);
bool IsExecuting();
bool ValidateSelection(int select, int unselect, SelectionSnapshot &snapshot, std::string &error);
bool RevalidateSelection(const SelectionSnapshot &snapshot, std::string &error);
bool ValidateDone(DoneSnapshot &snapshot, std::string &error);
bool RevalidateDone(const DoneSnapshot &snapshot, std::string &error);
void StartSelection(const SelectionSnapshot &snapshot);
void StartDone(const DoneSnapshot &snapshot);
void Reset();

} // namespace Menus::TraitsHandler
