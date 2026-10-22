#pragma once

// Engine-free core of the DirectInput hook: per-key script state and the injected-event queue. It uses no DirectInput
// or Windows types so the host tests can exercise it (tests/InputCoreTests.cpp); the hook in
// Hooks_DirectInput8Create.* only moves data between DirectInput and these types.

#include <cstddef>
#include <cstdint>
#include <deque>

namespace InputCore {

// Everything that can hold a control. A key is held while any owner holds it, so one owner releasing its press can
// never cancel another owner's press, and holding or releasing twice is harmless (it is one bit per owner).
enum class Owner : uint8_t { Walker, CombatAttack, CombatPulse, StancePulse, VatsPulse, Count };

static_assert(static_cast<size_t>(Owner::Count) <= 8, "owners are stored in one byte");

constexpr uint8_t OwnerBit(Owner owner) { return static_cast<uint8_t>(1u << static_cast<uint8_t>(owner)); }

struct KeyState {
  bool rawState = false;      // state from DirectInput last update
  bool gameState = false;     // state sent to the game last update
  bool insertedState = false; // true if a script held or tapped this key down last update

  uint8_t holders = 0; // OwnerBit mask of the owners holding the key down
  bool tap = false;    // key is being tapped (one update)
  bool userDisable = false;
  bool scriptDisable = false;

  bool IsHeld() const { return holders != 0; }
  void SetHeld(Owner owner, bool held) {
    if (held)
      holders = static_cast<uint8_t>(holders | OwnerBit(owner));
    else
      holders = static_cast<uint8_t>(holders & ~OwnerBit(owner));
  }
  // Drops every held and tapped state a script injected. The disable flags are configuration, not injected input.
  void ClearScripted() {
    holders = 0;
    tap = false;
  }

  // Applies the script state to the key's real state and returns what the game should see. `ignored` keys are never
  // filtered or injected.
  bool Process(bool keyDown, bool ignored) {
    insertedState = false;
    rawState = keyDown;

    if (!ignored) {
      if (userDisable)
        keyDown = false;

      if (!scriptDisable) {
        if (IsHeld())
          insertedState = true;

        if (tap) {
          insertedState = true;
          tap = false;
        }

        if (insertedState)
          keyDown = true;
      }
    }

    gameState = keyDown;
    return keyDown;
  }
};

// Events a script injects into a buffered device (the engine reads menus and the console through GetDeviceData).
// Bounded so an unread device cannot grow it without limit; a full queue drops the newest events.
template <class Event> class InjectedQueue {
public:
  static constexpr size_t kMaxQueued = 64;

  bool Push(const Event &event) {
    if (m_events.size() >= kMaxQueued)
      return false;
    m_events.push_back(event);
    return true;
  }
  // Both events or neither, so a press is never queued without its release.
  bool PushPair(const Event &first, const Event &second) {
    if (m_events.size() + 2 > kMaxQueued)
      return false;
    m_events.push_back(first);
    m_events.push_back(second);
    return true;
  }
  void Clear() { m_events.clear(); }
  bool Empty() const { return m_events.empty(); }
  size_t Size() const { return m_events.size(); }

  // Appends injected events after the `realCount` real events already in `out` (which has room for `requested`).
  // A null `out` only counts or discards. Peeking reports without consuming. Returns the injected events delivered.
  uint32_t Merge(Event *out, uint32_t realCount, uint32_t requested, bool peek) {
    const uint32_t room = requested - (realCount < requested ? realCount : requested);
    Event *virtualOut = out ? out + realCount : nullptr;

    uint32_t delivered = 0;
    if (peek) {
      delivered = static_cast<uint32_t>(m_events.size()) < room ? static_cast<uint32_t>(m_events.size()) : room;
      if (virtualOut) {
        for (uint32_t index = 0; index < delivered; ++index)
          virtualOut[index] = m_events[index];
      }
      return delivered;
    }
    while (delivered < room && !m_events.empty()) {
      if (virtualOut)
        virtualOut[delivered] = m_events.front();
      m_events.pop_front();
      ++delivered;
    }
    return delivered;
  }

private:
  std::deque<Event> m_events;
};

} // namespace InputCore
