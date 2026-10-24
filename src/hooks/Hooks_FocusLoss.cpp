#include "Hooks_FocusLoss.h"
#include "SafeWrite/SafeWrite.hpp"
#include <nvse/PluginAPI.h>

// All addresses are from the FalloutNV.exe 1.4.0.525 disassembly; none has been confirmed at runtime yet.
//
// `bAlwaysActive:General` (0x11DEED8) only keeps the frame-loop gate (0x86B363) running and forces the active flag in
// 0x871C90. It does not stop the window procedure (0x86A0A0) from pausing on WM_ACTIVATE with the WA_INACTIVE state:
// that branch calls 0x709470 with action 6 (queued: 0x7CB7D0 pauses the game, then HUD mode 4) when the game is active,
// no action is queued, 0x4F1540 is true and neither menu 0x3EF nor 0x3F5 is open, and only then calls 0x871C90(0).
// The `je` at 0x86A1F8 (on the 0x4F1540 result) already skips straight to 0x86A23C; making it unconditional skips the
// pause too while the 0x871C90(0) call still runs, so bAlwaysActive keeps deciding the active flag.
static constexpr uintptr_t kBackgroundSettingAddress = 0x11DEED8; // Setting bAlwaysActive:General, value at +4
static constexpr uintptr_t kPauseOnDeactivateJump = 0x86A1F8;
static constexpr uint8_t kJe = 0x74;
static constexpr uint8_t kJmp = 0xEB;
static constexpr uint8_t kJumpDisplacement = 0x42; // to 0x86A23C

void Hook_FocusLoss_Init() {
  const bool alwaysActive = *reinterpret_cast<const uint8_t *>(kBackgroundSettingAddress + 4) != 0;
  if (!alwaysActive) {
    _MESSAGE("Focus loss: bAlwaysActive is 0, leaving the game's pause on focus loss alone.");
    return;
  }

  const auto *site = reinterpret_cast<const uint8_t *>(kPauseOnDeactivateJump);
  if (site[0] != kJe || site[1] != kJumpDisplacement) {
    _ERROR("Focus loss: unexpected bytes %02X %02X at 0x%08X (another plugin or a different executable); pause on "
           "focus loss not suppressed.",
           site[0], site[1], static_cast<unsigned>(kPauseOnDeactivateJump));
    return;
  }

  HookUtils::SafeWrite8(kPauseOnDeactivateJump, kJmp);
  _MESSAGE("Focus loss: pause on window deactivation suppressed (bAlwaysActive=1).");
}
