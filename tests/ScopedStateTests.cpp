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
