#include "Hooks_FocusLoss.h"
#include "SafeWrite/SafeWrite.hpp"
#include <nvse/PluginAPI.h>

// All addresses are from the FalloutNV.exe 1.4.0.525 disassembly.
//
// `bAlwaysActive:General` (Setting at 0x11DEED8, value byte at +4, read through the getter 0x408D60) is read live every
// frame by the frame-loop gate (0x86B371: when the game is inactive and the value is 0 the frame is skipped, which is
// the main-menu freeze) and by the active-flag setter 0x871C90. The plugin forces it to 1 instead of relying on the
// INI.
//
// The setting does not stop the window procedure (0x86A0A0) from pausing on WM_ACTIVATE with the WA_INACTIVE state:
// that branch calls 0x709470 with action 6 (queued: 0x7CB7D0 pauses the game, then HUD mode 4) when the game is active,
// no action is queued, 0x4F1540 is true and neither menu 0x3EF nor 0x3F5 is open, and only then calls 0x871C90(0).
// The `je` at 0x86A1F8 (on the 0x4F1540 result) already skips straight to 0x86A23C; making it unconditional skips the
// pause too while the 0x871C90(0) call still runs.
static constexpr uintptr_t kAlwaysActiveValue = 0x11DEED8 + 4; // Setting bAlwaysActive:General, value at +4
static constexpr uintptr_t kPauseOnDeactivateJump = 0x86A1F8;
static constexpr uint8_t kJe = 0x74;
static constexpr uint8_t kJmp = 0xEB;
static constexpr uint8_t kJumpDisplacement = 0x42; // to 0x86A23C

void Hook_FocusLoss_Init() {
  auto *alwaysActive = reinterpret_cast<uint8_t *>(kAlwaysActiveValue);
  const uint8_t iniValue = *alwaysActive;
  *alwaysActive = 1;
  _MESSAGE("Focus loss: bAlwaysActive forced to 1 (INI value was %u).", static_cast<unsigned>(iniValue));

  const auto *site = reinterpret_cast<const uint8_t *>(kPauseOnDeactivateJump);
  if (site[0] == kJmp && site[1] == kJumpDisplacement) {
    _MESSAGE("Focus loss: pause on window deactivation already suppressed (patched by another plugin).");
    return;
  }
  if (site[0] != kJe || site[1] != kJumpDisplacement) {
    _ERROR("Focus loss: unexpected bytes %02X %02X at 0x%08X (another plugin or a different executable); pause on "
           "focus loss not suppressed.",
           site[0], site[1], static_cast<unsigned>(kPauseOnDeactivateJump));
    return;
  }

  HookUtils::SafeWrite8(kPauseOnDeactivateJump, kJmp);
  _MESSAGE("Focus loss: pause on window deactivation suppressed.");
}
