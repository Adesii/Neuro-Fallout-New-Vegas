#pragma once

// Engine-independent rules for telling Neuro who hurt her and how she died (todo 29): the per-attacker hit notes and
// the death report. CombatHandler feeds them engine observations; tests/DamageTests.cpp covers them. Part of the
// combat reporter family: the banded health/radiation reporter stays in CombatLogic.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace Combat {

// How a hit by the player's own thrown weapon, mine or explosion is named: not an actor, so the death report treats it
// as a hazard instead of naming her as her own killer.
inline constexpr const char *kSelfSource = "your own weapon or explosive";

struct HitInfo {
  uint32_t attackerId = 0;
  std::string attackerName; // empty when the source has no name
  std::string weapon;       // empty when unknown
  bool attackerIsActor = true;
};

// Remembers hits on the player and turns them into short notes, at most one per attacker per kPerAttackerInterval.
class HitNotes {
public:
  static constexpr float kPerAttackerInterval = 10.0f; // seconds

  struct LastHit {
    HitInfo info;
    float time = 0.0f;
  };

  void Reset();
  // The note for this hit, or nothing while the same attacker was already reported inside the interval. Every hit is
  // remembered for the death report whether or not it produced a note. `now` is a monotonic time in seconds.
  std::optional<std::string> Record(const HitInfo &hit, float now);
  const std::optional<LastHit> &Last() const { return m_last; }

private:
  std::unordered_map<uint32_t, float> m_lastReported;
  std::optional<LastHit> m_last;
};

enum class DeathCause { Actor, Radiation, Hazard, SuddenLoss, Unknown };

struct DeathInput {
  std::string killerName; // the engine's killer field, when it named an actor
  std::optional<HitNotes::LastHit> lastHit;
  float now = 0.0f;
  float radiation = 0.0f;
  float healthFractionBefore = 1.0f;        // the last sample before health reached zero, 0..1
  float healthFractionAtFightStart = -1.0f; // -1 unknown
  bool inCombat = false;
  std::string activity; // what she was doing: "attacking X", "walking to Y"; empty when nothing
};

// A hit this recent is taken as the cause when the engine does not name a killer.
inline constexpr float kRecentHitSeconds = 8.0f;
// Radiation at which "died of radiation" is the best guess when nothing hit her (1000 rads is lethal in FNV; unverified
// in this project, so a margin).
inline constexpr float kLethalRadiation = 900.0f;
// Entering a fight below this fraction of health earns the "check your health first" lesson.
inline constexpr float kReckless = 0.35f;
// How often a lesson is delivered as a roast instead of plain coaching.
inline constexpr int kRoastOneIn = 4;

DeathCause ClassifyDeath(const DeathInput &input);

// The fact, the cause and one lesson. Lessons are plain coaching most of the time and a playful roast about one death
// in kRoastOneIn, chosen with a seeded generator; a roast is only used where she had the information (low health she
// was told about, radiation stages she was told about, a fight she picked), never for hidden traps or falls.
class DeathReporter {
public:
  explicit DeathReporter(uint32_t seed = 0x9E3779B9u);
  std::string Describe(const DeathInput &input);

private:
  uint32_t m_state;
  uint32_t Next();
};

} // namespace Combat
