#include "DamageLogic.hpp"
#include <cmath>

namespace Combat {
namespace {

std::string AttackerLabel(const HitInfo &hit) {
  if (!hit.attackerName.empty())
    return hit.attackerName;
  return hit.attackerIsActor ? "Someone" : "Something";
}

std::string Percent(float fraction) { return std::to_string(static_cast<int>(std::lround(fraction * 100.0f))) + "%"; }

} // namespace

void HitNotes::Reset() {
  m_lastReported.clear();
  m_last.reset();
}

std::optional<std::string> HitNotes::Record(const HitInfo &hit, float now) {
  m_last = LastHit{hit, now};
  auto reported = m_lastReported.find(hit.attackerId);
  if (reported != m_lastReported.end() && now - reported->second < kPerAttackerInterval)
    return std::nullopt;
  m_lastReported[hit.attackerId] = now;
  if (!hit.attackerIsActor)
    return "You were hurt by " +
           (hit.attackerName.empty() ? std::string("something that is not a creature (a fall, an explosion, a trap or "
                                                   "fire)")
                                     : hit.attackerName) +
           ".";
  std::string note = AttackerLabel(hit) + " hits you";
  if (!hit.weapon.empty())
    note += " with " + hit.weapon;
  return note + ".";
}

DeathCause ClassifyDeath(const DeathInput &input) {
  if (!input.killerName.empty())
    return DeathCause::Actor;
  if (input.lastHit && input.now - input.lastHit->time <= kRecentHitSeconds)
    return input.lastHit->info.attackerIsActor ? DeathCause::Actor : DeathCause::Hazard;
  if (input.radiation >= kLethalRadiation)
    return DeathCause::Radiation;
  if (input.healthFractionBefore >= 0.4f)
    return DeathCause::SuddenLoss;
  return DeathCause::Unknown;
}

DeathReporter::DeathReporter(uint32_t seed) : m_state(seed ? seed : 1u) {}

uint32_t DeathReporter::Next() {
  m_state ^= m_state << 13;
  m_state ^= m_state >> 17;
  m_state ^= m_state << 5;
  return m_state;
}

std::string DeathReporter::Describe(const DeathInput &input) {
  const DeathCause cause = ClassifyDeath(input);
  const bool roast = Next() % kRoastOneIn == 0;

  std::string text = "## You died\nYou died. The game will reload your most recent save in a moment, so whatever "
                     "happened since then is undone.";

  std::string killer = input.killerName;
  std::string weapon;
  if (input.lastHit && input.lastHit->info.attackerIsActor) {
    if (killer.empty())
      killer = input.lastHit->info.attackerName;
    weapon = input.lastHit->info.weapon;
  }

  text += "\n\n**What killed you:** ";
  switch (cause) {
  case DeathCause::Actor:
    text += "killed by " + (killer.empty() ? std::string("an attacker you could not identify") : killer);
    if (!weapon.empty())
      text += " with " + weapon;
    text += ".";
    break;
  case DeathCause::Radiation:
    text += "radiation poisoning (" + std::to_string(static_cast<int>(input.radiation)) + " rads).";
    break;
  case DeathCause::Hazard:
    text += "something that is not a creature (an explosion, a trap or fire)" +
            (input.lastHit && !input.lastHit->info.attackerName.empty() ? " (" + input.lastHit->info.attackerName + ")"
                                                                        : std::string()) +
            ".";
    break;
  case DeathCause::SuddenLoss:
    text += "something took your health in one blow with nothing hitting you; most likely a fall, or an explosion.";
    break;
  case DeathCause::Unknown:
    text += "the game did not say, and nothing recent hit you.";
    break;
  }
  if (!input.activity.empty())
    text += " You were " + input.activity + ".";

  text += "\n\n**Lesson:** ";
  const bool wasReckless = input.healthFractionAtFightStart >= 0.0f && input.healthFractionAtFightStart < kReckless;
  switch (cause) {
  case DeathCause::Actor:
    if (wasReckless) {
      const std::string health = Percent(input.healthFractionAtFightStart);
      text += roast ? "You walked into that at " + health + " health. Bold strategy; how did it work out?"
                    : "You were at " + health +
                          " health when the fight started; check your health and heal before you engage.";
    } else {
      text += roast ? (killer.empty() ? std::string("That one clearly did not need a second try.")
                                      : killer + " did not need a second try. Maybe size up a fight with query_enemies "
                                                 "before picking it.")
                    : "Enemies can out-damage you. When your health gets low, retreat, heal (stimpak, food, "
                      "sleeping) and only then go back in; query_enemies shows what you are up against.";
    }
    break;
  case DeathCause::Radiation:
    text += roast ? "You were told about the radiation every 200 rads and kept standing in it. The wasteland has a "
                    "point."
                  : "Radiation builds up while you stay in irradiated areas. Leave them early and use RadAway before "
                    "it climbs this high.";
    break;
  case DeathCause::Hazard:
    text += "Traps and explosives are easy to miss. Keep your distance from mines, grenades and anything that looks "
            "wired, and watch your health when something goes off nearby.";
    break;
  case DeathCause::SuddenLoss:
    text += "Mind your footing around ledges and high places, and keep clear of explosives; a single mistake can "
            "cost all of your health at once.";
    break;
  case DeathCause::Unknown:
    text += "Check your health more often so you can react before it runs out.";
    break;
  }
  return text;
}

} // namespace Combat
