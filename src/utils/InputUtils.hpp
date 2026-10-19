#pragma once

#include <cstdint>
#include <vector>

namespace InputUtils {

inline constexpr uint32_t kNoControl = 0xFFFFFFFF;

// The DIHookControl macro index for the player's current binding of a game control: the mouse button when one
// is bound (offset past the 256 keyboard codes), otherwise the keyboard key. kNoControl when unbound.
uint32_t ResolveControl(int controlCode); // a ControlCode value

void SetControlHeld(uint32_t macro, bool held);

// Taps that last a few frames so the game's per-frame input polling sees them. Advance() every frame; ReleaseAll()
// on every exit path so no control is left held.
class PulseQueue {
public:
  void Pulse(uint32_t macro, float seconds);
  void Advance(float deltaTime);
  void ReleaseAll();
  bool Empty() const { return m_entries.empty(); }

private:
  struct Entry {
    uint32_t macro = kNoControl;
    float remaining = 0.0f;
  };
  std::vector<Entry> m_entries;
};

} // namespace InputUtils
