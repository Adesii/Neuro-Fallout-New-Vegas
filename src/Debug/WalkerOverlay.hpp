#pragma once

// Walker debug mode (todo 43): the `NWalkerDebug` console command and an on-screen overlay of the Walker's route,
// drawn over the finished frame from a Direct3D 9 Present hook. Off at start-up; nothing is hooked until it is first
// switched on, and while it is off the hook only checks a flag. It never changes what the Walker does.

#include "Movement/RouteDebug.hpp"

struct NVSEInterface;

namespace WalkerOverlay {

bool Enabled();
void SetEnabled(bool enabled);

// How much debug mode writes to neuro-fnv.log (the second number of the command). Off by default: the overlay alone is
// usually enough, and the route lines are many.
enum class LogLevel {
  Off = 0,     // only switching on/off and hook installation or failure
  Routes = 1,  // also a line per route change (every node) and the route's mode in the console
  Overlay = 2, // also the camera numbers and the overlay's one-time drawing diagnostics
};
LogLevel GetLogLevel();
inline bool Logs(LogLevel level) { return Enabled() && static_cast<int>(GetLogLevel()) >= static_cast<int>(level); }

// The Walker's latest view, copied for the next frame. Only called while Enabled().
void Publish(const RouteDebug::View &view);
// Forget the drawn view (load, new game, main menu, debug switched off).
void Clear();

// Every main loop iteration: installs the hook once debug is on and the renderer exists.
void Update();

// Registers NWalkerDebug. Call after the other commands: opcodes follow registration order.
void RegisterCommands(NVSEInterface *nvse);

} // namespace WalkerOverlay
