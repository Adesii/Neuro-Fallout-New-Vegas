#pragma once

#include "Actions/Action.hpp"
#include "CombatHandler.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace Menus::VatsHandler {

struct ShotRequest {
  uint32_t targetFormId = 0;
  uint32_t bodyPartId = 0;
  std::string targetName;
  std::string bodyPartName;
};

void Reset();
// Returns true while VATS owns the screen: opening, the menu, or the attack playback.
bool Process(bool unobstructed);
bool IsActive();

// open_vats: optionally aims at an enemy from query_enemies first so it is the initial selection.
Actions::PreparedAction PrepareOpen(std::optional<CombatHandler::TargetSelection> target);

// Window actions. Indexes are the one-based numbers of the currently published window.
bool ValidateShot(int targetIndex, int bodyPartIndex, ShotRequest &request, std::string &error);
bool RevalidateShot(const ShotRequest &request, std::string &error);
void StartShot(const ShotRequest &request);
bool ValidateExecute(std::string &error);
void StartExecute();
bool ValidateCancel(std::string &error);
void StartCancel();

} // namespace Menus::VatsHandler
