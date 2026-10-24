#include "Context/IdlePoker.hpp"
#include "TestFramework.hpp"
#include <algorithm>
#include <set>
#include <vector>

using Context::GreetingTimer;
using Context::IdlePoker;
using Context::PokeInput;

namespace {

constexpr float kFrame = 0.1f;

// Runs frames of `seconds` with the given flags and returns the number of pokes; stops at the first one if
// `untilPoke` and reports the elapsed time through `elapsed`.
int Run(IdlePoker &poker, float seconds, bool busy, bool acted = false, float *firstPokeAt = nullptr) {
  int pokes = 0;
  float time = 0.0f;
  for (int i = 0; i < static_cast<int>(seconds / kFrame + 0.5f); ++i) {
    time += kFrame;
    PokeInput input;
    input.dt = kFrame;
    input.busy = busy;
    input.neuroActed = acted && i == 0;
    if (poker.Update(input).poke) {
      if (pokes == 0 && firstPokeAt)
        *firstPokeAt = time;
      ++pokes;
    }
  }
  return pokes;
}

// Idle time until the next poke, starting right now.
float UntilNextPoke(IdlePoker &poker, float limit = 120.0f) {
  float time = 0.0f;
  while (time < limit) {
    time += kFrame;
    PokeInput input;
    input.dt = kFrame;
    if (poker.Update(input).poke)
      return time;
  }
  return -1.0f;
}

} // namespace

TEST("idle poke: the first poke comes after 6 to 10 seconds of idleness, whatever the seed") {
  for (uint32_t seed = 1; seed < 40; ++seed) {
    IdlePoker poker(seed);
    Run(poker, 1.0f, true); // settle: the session starts busy (loading)
    float at = -1.0f;
    Run(poker, 12.0f, false, false, &at);
    CHECK(at >= IdlePoker::kFirstIntervalMin - 0.2f && at <= IdlePoker::kFirstIntervalMax + 0.2f);
  }
}

TEST("idle poke: repeated pokes lengthen the interval towards the cap and never beyond it") {
  IdlePoker poker(7);
  Run(poker, 1.0f, true);
  float first = 0.0f;
  float longest = 0.0f;
  for (int i = 0; i < 8; ++i) {
    const float gap = UntilNextPoke(poker);
    CHECK(gap > 0.0f);
    longest = std::max(longest, gap);
    if (i >= 4) // 6 s * 1.7^4 is already beyond the cap
      CHECK(gap >= IdlePoker::kCapMin - 0.2f && gap <= IdlePoker::kCapMax + 0.2f);
    if (i == 0)
      first = gap;
    CHECK(gap >= first - 0.2f);
  }
  CHECK(longest <= IdlePoker::kCapMax + 0.2f);
  CHECK(poker.Repeats() >= 8);
  CHECK(UntilNextPoke(poker) >= IdlePoker::kCapMin - 0.2f);
}

TEST("idle poke: when Neuro acts the interval starts over") {
  IdlePoker poker(11);
  Run(poker, 1.0f, true);
  for (int i = 0; i < 5; ++i)
    UntilNextPoke(poker);
  CHECK(poker.Repeats() == 5);
  PokeInput acted;
  acted.dt = kFrame;
  acted.neuroActed = true;
  poker.Update(acted);
  CHECK(poker.Repeats() == 0);
  CHECK(poker.Threshold() <= IdlePoker::kFirstIntervalMax + 0.01f);
}

TEST("idle poke: nothing is sent while busy, and the idle clock restarts when the busy period ends") {
  IdlePoker poker(3);
  CHECK(Run(poker, 300.0f, true) == 0);
  float at = -1.0f;
  Run(poker, 12.0f, false, false, &at);
  CHECK(at >= IdlePoker::kFirstIntervalMin - 0.2f); // not carried over from before
}

TEST("idle poke: a short busy interruption restarts the count instead of adding up") {
  IdlePoker poker(5);
  Run(poker, 1.0f, true);
  int pokes = 0;
  for (int round = 0; round < 10; ++round) {
    pokes += Run(poker, 4.0f, false); // under the 6 s minimum
    pokes += Run(poker, 0.5f, true);
  }
  CHECK(pokes == 0);
}

TEST("idle poke: one second after Neuro's action ends the game it pokes, once") {
  IdlePoker poker(9);
  CHECK(Run(poker, 5.0f, true, true) == 0); // Neuro acted and a walk is running
  float at = -1.0f;
  const int pokes = Run(poker, 2.5f, false, false, &at);
  CHECK(pokes == 1);
  CHECK(at >= IdlePoker::kPostActionDelay - 0.01f && at <= IdlePoker::kPostActionDelay + 2 * kFrame);
}

TEST("idle poke: busy ending without anything Neuro did gets no quick poke (a load, a cutscene)") {
  IdlePoker poker(13);
  Run(poker, 5.0f, true);
  CHECK(Run(poker, 4.0f, false) == 0);
}

TEST("idle poke: the post-action poke is cancelled when the game turns busy again within the delay") {
  IdlePoker poker(17);
  Run(poker, 2.0f, true, true);
  CHECK(Run(poker, 0.5f, false) == 0);
  CHECK(Run(poker, 3.0f, true) == 0);
  CHECK(Run(poker, 0.5f, false) == 0); // no acted flag any more: back to plain idleness
}

TEST("idle poke: a prompt Neuro just received counts as activity") {
  IdlePoker poker(19);
  Run(poker, 1.0f, true);
  Run(poker, 5.0f, false);
  poker.NotifyPrompted();
  float at = -1.0f;
  Run(poker, 12.0f, false, false, &at);
  CHECK(at >= IdlePoker::kFirstIntervalMin - 0.2f);
}

TEST("idle poke: a prompt cancels the pending post-action poke") {
  IdlePoker poker(23);
  Run(poker, 2.0f, true, true);
  CHECK(Run(poker, 0.5f, false) == 0);
  poker.NotifyPrompted();
  CHECK(Run(poker, 1.0f, false) == 0);
}

TEST("idle poke: pokes are never closer than the minimum gap") {
  IdlePoker poker(29);
  std::vector<float> times;
  float now = 0.0f;
  auto step = [&](bool busy, bool acted) {
    now += kFrame;
    PokeInput input;
    input.dt = kFrame;
    input.busy = busy;
    input.neuroActed = acted;
    if (poker.Update(input).poke)
      times.push_back(now);
  };
  // Neuro keeps starting short actions: each one ends and asks for a post-action poke almost at once.
  for (int round = 0; round < 12; ++round) {
    step(true, true);
    for (int i = 0; i < 4; ++i)
      step(true, false);
    for (int i = 0; i < 12; ++i)
      step(false, false);
  }
  CHECK(times.size() >= 3);
  for (size_t i = 1; i < times.size(); ++i)
    CHECK(times[i] - times[i - 1] >= IdlePoker::kMinGap - 0.15f);
}

TEST("idle poke: phrases are Fallout-flavoured, never repeat back to back and carry no ids") {
  CHECK(IdlePoker::PhraseCount() >= 6);
  for (std::size_t i = 0; i < IdlePoker::PhraseCount(); ++i) {
    const std::string phrase = IdlePoker::Phrase(i);
    CHECK(!phrase.empty() && phrase.size() < 80);
    CHECK(phrase.find('`') == std::string::npos);
  }
  IdlePoker poker(31);
  Run(poker, 1.0f, true);
  std::set<std::size_t> seen;
  std::size_t last = 1000;
  for (int i = 0; i < 40; ++i) {
    float time = 0.0f;
    while (time < 100.0f) {
      time += kFrame;
      PokeInput input;
      input.dt = kFrame;
      const auto out = poker.Update(input);
      if (out.poke) {
        CHECK(out.phrase != last);
        last = out.phrase;
        seen.insert(out.phrase);
        break;
      }
    }
  }
  CHECK(seen.size() >= 4);
}

TEST("idle poke: reset clears the backoff and goes back to waiting for real idleness") {
  IdlePoker poker(37);
  Run(poker, 1.0f, true);
  for (int i = 0; i < 4; ++i)
    UntilNextPoke(poker);
  poker.Reset();
  CHECK(poker.Repeats() == 0);
  CHECK(Run(poker, 3.0f, false) == 0);
}

TEST("greeting timer: fires once every ten minutes and restarts when a greeting went out") {
  GreetingTimer timer;
  int fired = 0;
  for (int i = 0; i < 6000 * 3; ++i)
    fired += timer.Update(0.1f) ? 1 : 0;
  CHECK(fired == 3);
  GreetingTimer other;
  for (int i = 0; i < 5900; ++i)
    CHECK(!other.Update(0.1f));
  other.Sent();
  for (int i = 0; i < 5900; ++i)
    CHECK(!other.Update(0.1f));
  CHECK(other.Update(10.1f));
}
