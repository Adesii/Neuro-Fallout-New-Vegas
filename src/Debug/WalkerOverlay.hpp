#pragma once

// Walker debug mode (todo 43): the `NWalkerDebug` console command and an on-screen overlay of the Walker's route,
// drawn over the finished frame from a Direct3D 9 Present hook. Off at start-up; nothing is hooked until it is first
// switched on, and while it is off the hook only checks a flag. It never changes what the Walker does.

#include "Movement/RouteDebug.hpp"

struct NVSEInterface;

namespace WalkerOverlay {

bool Enabled();
void SetEnabled(bool enabled);

// The Walker's latest view, copied for the next frame. Only called while Enabled().
void Publish(const RouteDebug::View &view);
// Forget the drawn view (load, new game, main menu, debug switched off).
void Clear();

// Every main loop iteration: installs the hook once debug is on and the renderer exists.
void Update();

// Registers NWalkerDebug. Call after the other commands: opcodes follow registration order.
void RegisterCommands(NVSEInterface *nvse);

} // namespace WalkerOverlay
