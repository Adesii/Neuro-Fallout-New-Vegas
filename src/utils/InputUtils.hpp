#pragma once

#include "hooks/InputCore.hpp"
#include <cstdint>
#include <vector>

namespace InputUtils {

inline constexpr uint32_t kNoControl = 0xFFFFFFFF;

// The DIHookControl macro index for the player's current binding of a game control: the mouse button when one
// is bound (offset past the 256 keyboard codes), otherwise the keyboard key. kNoControl when unbound.
uint32_t ResolveControl(int controlCode); // a ControlCode value

// Holds or releases a control on behalf of one owner. The control stays down while any owner holds it, and repeating
// either call is harmless, so owners never cancel each other.
void SetControlHeld(InputCore::Owner owner, uint32_t macro, bool held);

// Taps that last a few frames so the game's per-frame input polling sees them. Advance() every frame; ReleaseAll()
// on every exit path so no control is left held. A pulse on a control that is already pulsing extends that pulse.
class PulseQueue {
public:
  explicit PulseQueue(InputCore::Owner owner) : m_owner(owner) {}
  void Pulse(uint32_t macro, float seconds);
  void Advance(float deltaTime);
  void ReleaseAll();
  bool Empty() const { return m_entries.empty(); }

private:
  InputCore::Owner m_owner;
  struct Entry {
    uint32_t macro = kNoControl;
    float remaining = 0.0f;
  };
  std::vector<Entry> m_entries;
};

} // namespace InputUtils
