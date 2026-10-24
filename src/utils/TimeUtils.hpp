#pragma once

namespace TimeUtils {

// Longest game frame the handlers integrate over: a load or a hitch must not turn into one huge step.
inline constexpr float kMaxFrameSeconds = 0.1f;

// The game's last frame length in seconds, clamped to 0..kMaxFrameSeconds; 0 when the game clock is not available.
// Game time stands still in menus (see NeuroSDK for wall-clock time).
float FrameSeconds();

} // namespace TimeUtils
