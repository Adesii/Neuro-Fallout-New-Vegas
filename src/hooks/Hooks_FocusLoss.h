#pragma once

// Keeps the game running and unpaused when its window loses focus, whatever Fallout.ini says: forces the engine's
// `bAlwaysActive:General` to 1 (so the frame loop never stops, main menu included) and removes the pause the window
// procedure queues on deactivation. Call once the INI has been loaded (kMessage_DeferredInit), so the INI cannot
// overwrite the forced value afterwards.
void Hook_FocusLoss_Init();
