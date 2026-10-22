#include "TestFramework.hpp"
#include "utils/ScopedState.hpp"

using namespace std::chrono_literals;
using ScopedState::Clock;

TEST("backoff: ready until a failure, then waits out its delay") {
  ScopedState::Backoff backoff(2s);
  const auto t0 = Clock::time_point{} + 100s;
  CHECK(backoff.Ready(t0));
  backoff.Fail(t0);
  CHECK(!backoff.Ready(t0));
  CHECK(!backoff.Ready(t0 + 1999ms));
  CHECK(backoff.Ready(t0 + 2s));
  backoff.Fail(t0 + 5s);
  CHECK(!backoff.Ready(t0 + 6s));
  backoff.Reset();
  CHECK(backoff.Ready(t0));
}

TEST("delay: restarts when the owner or signature changes") {
  ScopedState::Delay delay;
  int ownerA = 0;
  int ownerB = 0;
  CHECK(!delay.Ready(&ownerA, "x", 0ms)); // first sight only starts the clock
  CHECK(delay.Ready(&ownerA, "x", 0ms));
  CHECK(!delay.Ready(&ownerB, "x", 0ms));
  CHECK(!delay.Ready(&ownerB, "y", 0ms));
  CHECK(delay.Ready(&ownerB, "y", 0ms));
  delay.Reset();
  CHECK(!delay.Ready(&ownerB, "y", 0ms));
}

TEST("delay: a long delay is not ready right away") {
  ScopedState::Delay delay;
  int owner = 0;
  CHECK(!delay.Ready(&owner, "x", 1h));
  CHECK(!delay.Ready(&owner, "x", 1h));
}

TEST("observation: remembers one owner and signature until reset") {
  ScopedState::Observation observation;
  int owner = 0;
  int other = 0;
  CHECK(!observation.IsCurrent(&owner, "a"));
  observation.Commit(&owner, "a");
  CHECK(observation.IsCurrent(&owner, "a"));
  CHECK(!observation.IsCurrent(&owner, "b"));
  CHECK(!observation.IsCurrent(&other, "a"));
  observation.Reset();
  CHECK(!observation.IsCurrent(&owner, "a"));
}

TEST("step timer: counts only the time the operation is running") {
  ScopedState::StepTimer timer;
  const auto t0 = ScopedState::Clock::now();
  timer.Start(t0);
  timer.Tick(true, t0 + 400ms);
  timer.Tick(true, t0 + 900ms);
  CHECK(timer.Elapsed() == 900ms);
  CHECK(!timer.Expired(900ms));
  CHECK(timer.Expired(800ms));
}

TEST("step timer: an obstruction does not count and the first frame back adds nothing") {
  ScopedState::StepTimer timer;
  const auto t0 = ScopedState::Clock::now();
  timer.Start(t0);
  timer.Tick(true, t0 + 500ms);
  // A popup covers the menu for a long time; the handler keeps ticking every frame.
  for (int frame = 1; frame <= 600; ++frame)
    timer.Tick(false, t0 + 500ms + frame * 100ms);
  const auto back = t0 + 500ms + 600 * 100ms;
  timer.Tick(true, back + 16ms);
  CHECK(timer.Elapsed() == 500ms);
  timer.Tick(true, back + 32ms);
  CHECK(timer.Elapsed() == 516ms);
  CHECK(!timer.Expired(10s));
}

TEST("step timer: frames that were never ticked (a stall) are not counted either") {
  ScopedState::StepTimer timer;
  const auto t0 = ScopedState::Clock::now();
  timer.Start(t0);
  timer.Tick(true, t0 + 100ms);
  timer.Tick(true, t0 + 100ms + 5min); // nothing ticked for five minutes
  CHECK(timer.Elapsed() == 100ms);
}

TEST("step timer: unstarted timers never expire and reset clears them") {
  ScopedState::StepTimer timer;
  const auto t0 = ScopedState::Clock::now();
  timer.Tick(true, t0 + 5s);
  CHECK(!timer.Started());
  CHECK(!timer.Expired(0ms));
  timer.Start(t0);
  timer.Tick(true, t0 + 500ms);
  timer.Reset();
  CHECK(!timer.Started());
  CHECK(timer.Elapsed() == 0ms);
}
