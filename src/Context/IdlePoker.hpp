#pragma once

#include <cstddef>
#include <cstdint>

namespace Context {

// When to nudge Neuro because she has been doing nothing, and when to repeat the greeting. Engine-free
// (tests/IdlePokerTests.cpp); NeuroSDK::MainLoop owns the single instance and feeds it once per frame, after the
// handlers ran. See docs/design/idle-poke.md.
//
// A poke is a generic, non-silent context line that carries no game state (the phrase set is in IdlePoker.cpp).
// Two things trigger it: a stretch of true idleness, and a short delay after the game stops being busy once Neuro had
// acted (her walk or fight ended, her menu closed). Repeated pokes space out and the spacing resets as soon as Neuro
// acts.
struct PokeInput {
  float dt = 0.0f;
  // Something other than Neuro's decision is in progress: Walker or combat running, a menu, VATS, the Pip-Boy, a
  // pending action result, an outstanding force, loading, no game loaded. Nothing is sent while this is true.
  bool busy = false;
  // An action arrived from Neuro this frame.
  bool neuroActed = false;
};

struct PokeOutput {
  bool poke = false;
  std::size_t phrase = 0; // index for IdlePoker::Phrase
};

class IdlePoker {
public:
  static constexpr float kFirstIntervalMin = 6.0f; // seconds of idleness before the first poke (jittered)
  static constexpr float kFirstIntervalMax = 10.0f;
  static constexpr float kGrowth = 1.7f;  // each repeat multiplies the interval
  static constexpr float kCapMin = 30.0f; // the interval stops growing at a value drawn from this range
  static constexpr float kCapMax = 60.0f;
  static constexpr float kPostActionDelay = 1.0f; // after the game stops being busy
  static constexpr float kMinGap = 3.0f;          // never two pokes closer than this, whatever the trigger

  explicit IdlePoker(uint32_t seed = 0x2545F491u);

  // Back to a fresh state (load, new game, disconnect). Keeps the random sequence.
  void Reset();
  PokeOutput Update(const PokeInput &input);
  // A non-silent context message reached Neuro: she has just been prompted, so a nudge now would be redundant.
  void NotifyPrompted();

  static std::size_t PhraseCount();
  static const char *Phrase(std::size_t index);

  // For tests and logs.
  float Threshold() const { return m_threshold; }
  int Repeats() const { return m_repeats; }

private:
  uint32_t m_state;
  float m_idle = 0.0f;
  float m_sinceLastPoke = 0.0f;
  float m_postAction = -1.0f; // negative: no post-action poke pending
  float m_threshold = 0.0f;
  int m_repeats = 0;
  bool m_wasBusy = true; // starts busy so a fresh session waits for the first real idleness
  bool m_actedSinceBusyEnd = false;
  bool m_hasLastPhrase = false;
  std::size_t m_lastPhrase = 0;

  float Uniform(float low, float high);
  float NextThreshold();
  PokeOutput Fire();
};

// Repeats the greeting on a fixed period. The first greeting of a session is requested by the caller (connect, load,
// new game); this only supplies the periodic one.
class GreetingTimer {
public:
  static constexpr float kPeriod = 600.0f;

  // True once per period.
  bool Update(float dt);
  // A greeting went out by any route: restart the period.
  void Sent() { m_elapsed = 0.0f; }

private:
  float m_elapsed = 0.0f;
};

} // namespace Context
