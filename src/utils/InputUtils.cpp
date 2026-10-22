#include "InputUtils.hpp"
#include "GameOSDepend.h"
#include "hooks/Hooks_DirectInput8Create.h"

namespace InputUtils {
namespace {

constexpr uint8_t kUnboundBinding = 0xFF;
constexpr uint8_t kMouseButtonCount = 8;

} // namespace

uint32_t ResolveControl(int controlCode) {
  auto *input = OSInputGlobals::GetSingleton();
  if (!input)
    return kNoControl;
  // Mouse bindings are the DirectInput button index (0 = left); FalloutNVAccess/src/AutoAim.cpp reads them the
  // same way. The input hook indexes mouse buttons after the 256 keyboard codes.
  const uint8_t mouse = input->mouseBinds[controlCode];
  if (mouse != kUnboundBinding && mouse < kMouseButtonCount)
    return kMacro_MouseButtonOffset + mouse;
  const uint8_t key = input->keyBinds[controlCode];
  return key != kUnboundBinding ? key : kNoControl;
}

void SetControlHeld(InputCore::Owner owner, uint32_t macro, bool held) {
  if (macro != kNoControl)
    DIHookControl::GetSingleton().SetKeyHeldState(owner, macro, held);
}

} // namespace InputUtils
