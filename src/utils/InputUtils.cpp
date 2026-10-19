#include "InputUtils.hpp"
#include "GameOSDepend.h"
#include "hooks/Hooks_DirectInput8Create.h"
#include <algorithm>

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

void SetControlHeld(uint32_t macro, bool held) {
  if (macro != kNoControl)
    DIHookControl::GetSingleton().SetKeyHeldState(macro, held);
}

void PulseQueue::Pulse(uint32_t macro, float seconds) {
  if (macro == kNoControl)
    return;
  SetControlHeld(macro, true);
  m_entries.push_back({macro, seconds});
}

void PulseQueue::Advance(float deltaTime) {
  for (auto &entry : m_entries) {
    entry.remaining -= deltaTime;
    if (entry.remaining <= 0.0f)
      SetControlHeld(entry.macro, false);
  }
  std::erase_if(m_entries, [](const Entry &entry) { return entry.remaining <= 0.0f; });
}

void PulseQueue::ReleaseAll() {
  for (const auto &entry : m_entries)
    SetControlHeld(entry.macro, false);
  m_entries.clear();
}

} // namespace InputUtils
