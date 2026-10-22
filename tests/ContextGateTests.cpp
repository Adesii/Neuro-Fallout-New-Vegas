#include "Context/ContextGate.hpp"
#include "Menus/SubtitleTracker.hpp"
#include "TestFramework.hpp"

using namespace std::chrono_literals;
using Context::ContextGate;
using Verdict = ContextGate::Verdict;

namespace {
const auto t0 = ContextGate::Clock::time_point{} + 1000s;
}

TEST("context gate: blank and bracket-only text is dropped as empty") {
  ContextGate gate;
  CHECK(gate.Check("", false, t0) == Verdict::Empty);
  CHECK(gate.Check("  \n\t", false, t0) == Verdict::Empty);
  CHECK(gate.Check("[]", true, t0) == Verdict::Empty);
  CHECK(gate.Check(" [] ", true, t0) == Verdict::Empty);
  CHECK(gate.Check("[x]", true, t0) == Verdict::Send);
}

TEST("context gate: a repeat inside the window is a duplicate, after it the message passes again") {
  ContextGate gate;
  CHECK(gate.Check("Combat ended", false, t0) == Verdict::Send);
  CHECK(gate.Check("Combat ended", false, t0 + 1s) == Verdict::Duplicate);
  CHECK(gate.Check("Combat ended", false, t0 + ContextGate::kDuplicateWindow) == Verdict::Duplicate);
  CHECK(gate.Check("Combat ended", false, t0 + ContextGate::kDuplicateWindow + 1ms) == Verdict::Send);
}

TEST("context gate: only a consecutive repeat counts, and silence is part of the identity") {
  ContextGate gate;
  CHECK(gate.Check("a", false, t0) == Verdict::Send);
  CHECK(gate.Check("a", true, t0 + 100ms) == Verdict::Send);
  CHECK(gate.Check("b", true, t0 + 200ms) == Verdict::Send);
  CHECK(gate.Check("a", true, t0 + 300ms) == Verdict::Send); // b came in between
  CHECK(gate.Check("a", true, t0 + 400ms) == Verdict::Duplicate);
}

TEST("context gate: a duplicate does not extend the window") {
  ContextGate gate;
  CHECK(gate.Check("a", false, t0) == Verdict::Send);
  CHECK(gate.Check("a", false, t0 + 2s) == Verdict::Duplicate);
  CHECK(gate.Check("a", false, t0 + 4s) == Verdict::Send); // measured from the message that was actually sent
}

TEST("context gate: the outage warning fires once per outage and a new connection re-arms it") {
  ContextGate gate;
  CHECK(gate.ShouldWarnOutage());
  CHECK(!gate.ShouldWarnOutage());
  CHECK(!gate.ShouldWarnOutage());
  gate.Connected();
  CHECK(gate.ShouldWarnOutage());
}

TEST("context gate: a new connection forgets the last message so the fresh status is never swallowed") {
  ContextGate gate;
  CHECK(gate.Check("status", false, t0) == Verdict::Send);
  gate.Connected();
  CHECK(gate.Check("status", false, t0 + 1s) == Verdict::Send);
}

namespace {
Menus::SubtitleEntry Line(const char *text, uint32_t start = 1, int32_t priority = 0) {
  return {text, start, priority};
}
} // namespace

TEST("subtitle tracker: reports an entry once while it stays on screen") {
  Menus::SubtitleTracker tracker;
  CHECK_EQ(tracker.Observe({Line("Hello")}).size(), size_t{1});
  CHECK(tracker.Observe({Line("Hello")}).empty());
  CHECK(tracker.Observe({Line("Hello")}).empty());
}

TEST("subtitle tracker: only the new entries of a changed array are reported") {
  Menus::SubtitleTracker tracker;
  tracker.Observe({Line("one", 1)});
  const auto fresh = tracker.Observe({Line("one", 1), Line("two", 2)});
  REQUIRE(fresh.size() == 1);
  CHECK(fresh[0].text == "two");
}

TEST("subtitle tracker: a line that left the screen is reported again when it returns") {
  Menus::SubtitleTracker tracker;
  tracker.Observe({Line("Bark", 100)});
  CHECK(tracker.Observe({}).empty());
  CHECK_EQ(tracker.Observe({Line("Bark", 100)}).size(), size_t{1}); // e.g. a start time that restarted after a load
}

TEST("subtitle tracker: reset forgets what was seen, and empty or repeated entries in one frame are ignored") {
  Menus::SubtitleTracker tracker;
  tracker.Observe({Line("a")});
  tracker.Reset();
  CHECK_EQ(tracker.Observe({Line("a")}).size(), size_t{1});
  Menus::SubtitleTracker other;
  CHECK_EQ(other.Observe({Line(""), Line("x"), Line("x")}).size(), size_t{1});
}
