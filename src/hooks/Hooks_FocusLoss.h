#pragma once

// Stops the game from pausing itself when its window loses focus. Needs `bAlwaysActive=1` under [General] in
// Fallout.ini; with the setting off the patch is not installed and the game behaves as in vanilla. Call once the INI
// has been loaded (kMessage_DeferredInit). Changing the setting at runtime does not install or remove the patch.
void Hook_FocusLoss_Init();
