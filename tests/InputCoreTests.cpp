#include "TestFramework.hpp"
#include "hooks/InputCore.hpp"
#include "utils/InputUtils.hpp"
#include <map>

using InputCore::InjectedQueue;
using InputCore::KeyState;
using InputCore::Owner;

namespace {

// A stand-in for the hook's key table, fed by InputUtils::SetControlHeld through the test definition below.
std::map<uint32_t, KeyState> g_keys;

bool Down(uint32_t macro) { return g_keys[macro].IsHeld(); }

} // namespace

namespace InputUtils {
// The real definition forwards to the DirectInput hook; the host tests route it to the engine-free key state.
void SetControlHeld(Owner owner, uint32_t macro, bool held) {
  if (macro != kNoControl)
    g_keys[macro].SetHeld(owner, held);
}
} // namespace InputUtils

TEST("key state: a held key is injected and a tap lasts one update") {
  KeyState key;
  CHECK(!key.Process(false, false));
  key.SetHeld(Owner::Walker, true);
  CHECK(key.Process(false, false));
  CHECK(key.insertedState);
  key.SetHeld(Owner::Walker, false);
  CHECK(!key.Process(false, false));

  key.tap = true;
  CHECK(key.Process(false, false));
  CHECK(!key.Process(false, false));
}

TEST("key state: real input passes through, user disable blocks it, script disable blocks injection") {
  KeyState key;
  CHECK(key.Process(true, false));
  CHECK(key.rawState);

  key.userDisable = true;
  CHECK(!key.Process(true, false));
  CHECK(key.rawState); // the raw state still records what the user did
  key.SetHeld(Owner::Walker, true);
  CHECK(key.Process(false, false)); // a script can still press a user-disabled key

  key.scriptDisable = true;
  CHECK(!key.Process(false, false));

  KeyState ignored;
  ignored.userDisable = true;
  CHECK(ignored.Process(true, true)); // ignored keys are never filtered
}

TEST("key state: owners hold independently, so one release never cancels another") {
  KeyState key;
  key.SetHeld(Owner::Walker, true);
  key.SetHeld(Owner::CombatPulse, true);
  key.SetHeld(Owner::Walker, false);
  CHECK(key.IsHeld());
  key.SetHeld(Owner::Walker, false); // releasing twice changes nothing
  CHECK(key.IsHeld());
  key.SetHeld(Owner::CombatPulse, false);
  CHECK(!key.IsHeld());

  key.SetHeld(Owner::VatsPulse, true);
  key.SetHeld(Owner::VatsPulse, true); // holding twice is still one hold
  key.SetHeld(Owner::VatsPulse, false);
  CHECK(!key.IsHeld());
}

TEST("key state: clearing drops holds and taps but keeps the disable flags") {
  KeyState key;
  key.SetHeld(Owner::CombatAttack, true);
  key.SetHeld(Owner::StancePulse, true);
  key.tap = true;
  key.scriptDisable = true;
  key.ClearScripted();
  CHECK(!key.IsHeld());
  CHECK(!key.tap);
  CHECK(key.scriptDisable);
}

TEST("injected queue: injected events follow the real ones in the caller's buffer") {
  InjectedQueue<int> queue;
  queue.Push(10);
  queue.Push(11);
  queue.Push(12);
  int out[4] = {1, 2, 0, 0};
  CHECK_EQ(queue.Merge(out, 2, 4, false), 2u);
  CHECK_EQ(out[2], 10);
  CHECK_EQ(out[3], 11);
  CHECK_EQ(queue.Size(), size_t{1}); // the rest waits for the next read
}

TEST("injected queue: peeking reports without consuming") {
  InjectedQueue<int> queue;
  queue.Push(5);
  queue.Push(6);
  int out[3] = {0, 0, 0};
  CHECK_EQ(queue.Merge(out, 0, 3, true), 2u);
  CHECK_EQ(out[0], 5);
  CHECK_EQ(out[1], 6);
  CHECK_EQ(queue.Size(), size_t{2});
  CHECK_EQ(queue.Merge(out, 0, 3, false), 2u);
  CHECK(queue.Empty());
}

TEST("injected queue: a full buffer of real events leaves no room and a null buffer only counts") {
  InjectedQueue<int> queue;
  queue.Push(1);
  int out[2] = {0, 0};
  CHECK_EQ(queue.Merge(out, 2, 2, false), 0u);
  CHECK_EQ(queue.Size(), size_t{1});
  CHECK_EQ(queue.Merge(nullptr, 0, 5, true), 1u);
  CHECK_EQ(queue.Size(), size_t{1});
  CHECK_EQ(queue.Merge(nullptr, 0, 5, false), 1u); // discarding consumes
  CHECK(queue.Empty());
}

TEST("injected queue: a device with no real events still delivers injected ones") {
  // The hook reports zero real events when the device is unavailable (alt-tab); injected input must still arrive.
  InjectedQueue<int> queue;
  queue.Push(9);
  int out[2] = {0, 0};
  CHECK_EQ(queue.Merge(out, 0, 2, false), 1u);
  CHECK_EQ(out[0], 9);
}

TEST("injected queue: bounded, drops the newest, and pairs are all or nothing") {
  InjectedQueue<int> queue;
  for (size_t index = 0; index < InjectedQueue<int>::kMaxQueued - 1; ++index)
    CHECK(queue.Push(1));
  CHECK(!queue.PushPair(2, 3)); // only one slot left: neither is queued
  CHECK_EQ(queue.Size(), InjectedQueue<int>::kMaxQueued - 1);
  CHECK(queue.Push(4));
  CHECK(!queue.Push(5));
  CHECK_EQ(queue.Size(), InjectedQueue<int>::kMaxQueued);
  queue.Clear();
  CHECK(queue.Empty());
  CHECK(queue.PushPair(2, 3));
}

TEST("pulse queue: a pulse holds the control until it expires") {
  g_keys.clear();
  InputUtils::PulseQueue pulses(Owner::CombatPulse);
  pulses.Pulse(7, 0.1f);
  CHECK(Down(7));
  pulses.Advance(0.05f);
  CHECK(Down(7));
  pulses.Advance(0.06f);
  CHECK(!Down(7));
  CHECK(pulses.Empty());
}

TEST("pulse queue: pulsing a control that is already pulsing refreshes it instead of releasing early") {
  g_keys.clear();
  InputUtils::PulseQueue pulses(Owner::VatsPulse);
  pulses.Pulse(7, 0.1f);
  pulses.Advance(0.08f);
  pulses.Pulse(7, 0.1f); // second press while the first is still running
  pulses.Advance(0.05f); // the first entry would have expired by now
  CHECK(Down(7));
  pulses.Advance(0.06f);
  CHECK(!Down(7));
  CHECK(pulses.Empty());
}

TEST("pulse queue: a shorter second pulse never shortens a running one") {
  g_keys.clear();
  InputUtils::PulseQueue pulses(Owner::VatsPulse);
  pulses.Pulse(7, 0.2f);
  pulses.Pulse(7, 0.05f);
  pulses.Advance(0.1f);
  CHECK(Down(7));
}

TEST("pulse queue: two queues on the same control do not cancel each other") {
  g_keys.clear();
  InputUtils::PulseQueue combat(Owner::CombatPulse);
  InputUtils::PulseQueue stance(Owner::StancePulse);
  combat.Pulse(7, 0.05f);
  stance.Pulse(7, 0.2f);
  combat.Advance(0.1f); // combat's pulse ended, stance's is still running
  CHECK(Down(7));
  stance.ReleaseAll();
  CHECK(!Down(7));
}

TEST("pulse queue: release all drops every control, and unbound controls are ignored") {
  g_keys.clear();
  InputUtils::PulseQueue pulses(Owner::StancePulse);
  pulses.Pulse(1, 1.0f);
  pulses.Pulse(2, 1.0f);
  pulses.Pulse(InputUtils::kNoControl, 1.0f);
  pulses.ReleaseAll();
  CHECK(!Down(1));
  CHECK(!Down(2));
  CHECK(pulses.Empty());
  pulses.ReleaseAll(); // safe to repeat
}
