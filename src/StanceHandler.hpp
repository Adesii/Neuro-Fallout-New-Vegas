#pragma once

#include <string>

// The player's stance: `crouch_toggle` taps the Sneak control and reports the sneak state the engine then shows.
namespace StanceHandler {

void Process(bool gameplayBlocked);
void Reset();
bool ValidateCrouchToggle(std::string &error);
void ToggleCrouch();

} // namespace StanceHandler
