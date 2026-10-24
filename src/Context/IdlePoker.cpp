#include "IdlePoker.hpp"
#include <algorithm>
#include <cmath>

namespace Context {
namespace {

// Short, stateless, Fallout-flavoured nudges: no game state in the text, no suggestions (Skyrim's integration drops
// its suggestions on purpose, they steer her into a rut).
constexpr const char *kPhrases[] = {
    "The wasteland waits. What do you do next?",
    "Another quiet moment in the Mojave. What's your next move?",
    "The road ahead is yours to choose.",
    "Nothing is stopping you. Pick your next action.",
    "The desert doesn't wait forever. Decide what to do next.",
    "Still standing out here, Courier? Do something.",
};

} // namespace

IdlePoker::IdlePoker(uint32_t seed) : m_state(seed ? seed : 1u) { Reset(); }

float IdlePoker::Uniform(float low, float high) {
  // xorshift32; the top 24 bits make an exact float in [0, 1).
  m_state ^= m_state << 13;
  m_state ^= m_state >> 17;
  m_state ^= m_state << 5;
  const float unit = static_cast<float>(m_state >> 8) / 16777216.0f;
  return low + (high - low) * unit;
}

float IdlePoker::NextThreshold() {
  const float base = Uniform(kFirstIntervalMin, kFirstIntervalMax);
  const float grown = base * std::pow(kGrowth, static_cast<float>(m_repeats));
  return std::min(grown, Uniform(kCapMin, kCapMax));
}

void IdlePoker::Reset() {
  m_idle = 0.0f;
  m_sinceLastPoke = kMinGap;
  m_postAction = -1.0f;
  m_repeats = 0;
  m_wasBusy = true;
  m_actedSinceBusyEnd = false;
  m_hasLastPhrase = false;
  m_threshold = NextThreshold();
}

void IdlePoker::NotifyPrompted() {
  m_idle = 0.0f;
  m_postAction = -1.0f;
}

PokeOutput IdlePoker::Fire() {
  PokeOutput output;
  output.poke = true;
  // Never the same phrase twice in a row.
  std::size_t index = static_cast<std::size_t>(Uniform(0.0f, static_cast<float>(PhraseCount()))) % PhraseCount();
  if (m_hasLastPhrase && index == m_lastPhrase)
    index = (index + 1) % PhraseCount();
  m_lastPhrase = index;
  m_hasLastPhrase = true;
  output.phrase = index;

  ++m_repeats;
  m_idle = 0.0f;
  m_sinceLastPoke = 0.0f;
  m_postAction = -1.0f;
  m_threshold = NextThreshold();
  return output;
}

PokeOutput IdlePoker::Update(const PokeInput &input) {
  m_sinceLastPoke += input.dt;
  if (input.neuroActed) {
    m_repeats = 0;
    m_idle = 0.0f;
    m_postAction = -1.0f;
    m_actedSinceBusyEnd = true;
    m_threshold = NextThreshold();
  }
  if (input.busy) {
    // Whatever is going on resets the idle clock; a post-action poke is scheduled once it is over.
    m_idle = 0.0f;
    m_postAction = -1.0f;
    m_wasBusy = true;
    return {};
  }
  if (m_wasBusy) {
    m_wasBusy = false;
    // Only what Neuro started counts as "an action finished"; loading a save or a cutscene ending is not one.
    if (m_actedSinceBusyEnd)
      m_postAction = kPostActionDelay;
    m_actedSinceBusyEnd = false;
  }

  m_idle += input.dt;
  if (m_postAction >= 0.0f) {
    m_postAction -= input.dt;
    if (m_postAction < 0.0f) {
      if (m_sinceLastPoke >= kMinGap)
        return Fire();
      m_postAction = 0.0f; // wait out the minimum gap, then fire on the first frame it is over
    }
  }
  if (m_postAction < 0.0f && m_idle >= m_threshold && m_sinceLastPoke >= kMinGap)
    return Fire();
  return {};
}

std::size_t IdlePoker::PhraseCount() { return sizeof(kPhrases) / sizeof(kPhrases[0]); }

const char *IdlePoker::Phrase(std::size_t index) { return kPhrases[index % PhraseCount()]; }

bool GreetingTimer::Update(float dt) {
  m_elapsed += dt;
  if (m_elapsed < kPeriod)
    return false;
  m_elapsed = 0.0f;
  return true;
}

} // namespace Context
