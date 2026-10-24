#include "Combat/CombatLogic.hpp"
#include "Combat/DamageLogic.hpp"
#include "TestFramework.hpp"

using namespace Combat;

namespace {

HitInfo Hit(uint32_t id, const char *name, const char *weapon = "", bool actor = true) {
  return {id, name, weapon, actor};
}

DeathInput KilledBy(const char *name) {
  DeathInput input;
  input.killerName = name;
  return input;
}

} // namespace

TEST("hit notes: one note per attacker per ten seconds, others unaffected") {
  HitNotes notes;
  auto first = notes.Record(Hit(1, "Powder Ganger", "9mm pistol"), 0.0f);
  REQUIRE(first.has_value());
  CHECK_EQ(*first, std::string("Powder Ganger hits you with 9mm pistol."));
  CHECK(!notes.Record(Hit(1, "Powder Ganger", "9mm pistol"), 3.0f).has_value());
  CHECK(!notes.Record(Hit(1, "Powder Ganger", "9mm pistol"), 9.9f).has_value());
  CHECK(notes.Record(Hit(2, "Gecko"), 4.0f).has_value());
  CHECK(notes.Record(Hit(1, "Powder Ganger", "9mm pistol"), 10.0f).has_value());
  // The interval counts from the last note, not from the last suppressed hit.
  CHECK(!notes.Record(Hit(1, "Powder Ganger"), 15.0f).has_value());
  CHECK(notes.Record(Hit(1, "Powder Ganger"), 20.0f).has_value());
}

TEST("hit notes: unnamed attackers and non-actor sources read sensibly") {
  HitNotes notes;
  CHECK_EQ(*notes.Record(Hit(1, ""), 0.0f), std::string("Someone hits you."));
  CHECK_EQ(*notes.Record(Hit(2, "Frag Mine", "", false), 0.0f), std::string("You were hurt by Frag Mine."));
  CHECK(notes.Record(Hit(3, "", "", false), 0.0f)->find("not a creature") != std::string::npos);
}

TEST("hit notes: every hit is remembered for the death report, reset forgets them") {
  HitNotes notes;
  notes.Record(Hit(1, "Ganger"), 0.0f);
  notes.Record(Hit(1, "Ganger", "pistol"), 2.0f); // suppressed as a note
  REQUIRE(notes.Last().has_value());
  CHECK(notes.Last()->time == 2.0f);
  CHECK_EQ(notes.Last()->info.weapon, std::string("pistol"));
  notes.Reset();
  CHECK(!notes.Last().has_value());
  CHECK(notes.Record(Hit(1, "Ganger"), 3.0f).has_value());
}

TEST("death cause: the engine's killer wins, then a recent hit, then radiation, then a sudden drop") {
  DeathInput input = KilledBy("Viper");
  input.lastHit = HitNotes::LastHit{Hit(1, "Someone else", "", false), 0.0f};
  CHECK(ClassifyDeath(input) == DeathCause::Actor);

  input = {};
  input.now = 5.0f;
  input.lastHit = HitNotes::LastHit{Hit(1, "Ganger"), 1.0f};
  CHECK(ClassifyDeath(input) == DeathCause::Actor);
  input.lastHit = HitNotes::LastHit{Hit(1, "Frag Mine", "", false), 1.0f};
  CHECK(ClassifyDeath(input) == DeathCause::Hazard);
  input.lastHit = HitNotes::LastHit{Hit(1, "Ganger"), -20.0f}; // too long ago
  input.radiation = 950.0f;
  CHECK(ClassifyDeath(input) == DeathCause::Radiation);
  input.radiation = 100.0f;
  input.healthFractionBefore = 0.8f;
  CHECK(ClassifyDeath(input) == DeathCause::SuddenLoss);
  input.healthFractionBefore = 0.1f;
  CHECK(ClassifyDeath(input) == DeathCause::Unknown);
}

TEST("death report: fact, cause and one lesson; names the killer, weapon and what she was doing") {
  DeathReporter reporter(1);
  DeathInput input = KilledBy("Powder Ganger");
  input.lastHit = HitNotes::LastHit{Hit(1, "Powder Ganger", "9mm pistol"), 0.0f};
  input.activity = "attacking Powder Ganger";
  const std::string text = reporter.Describe(input);
  CHECK(text.find("You died") != std::string::npos);
  CHECK(text.find("reload") != std::string::npos);
  CHECK(text.find("killed by Powder Ganger with 9mm pistol") != std::string::npos);
  CHECK(text.find("You were attacking Powder Ganger.") != std::string::npos);
  CHECK(text.find("**Lesson:**") != std::string::npos);
  CHECK(text.find("##") == 0);
}

TEST("death report: a reckless entry is called out with the health she started at") {
  DeathReporter reporter(2);
  DeathInput input = KilledBy("Viper");
  input.healthFractionAtFightStart = 0.12f;
  const std::string text = reporter.Describe(input);
  CHECK(text.find("12%") != std::string::npos);
}

TEST("death report: radiation, hazards, falls and unknown causes each get their own wording") {
  DeathReporter reporter(3);
  DeathInput radiation;
  radiation.radiation = 980.0f;
  CHECK(reporter.Describe(radiation).find("radiation poisoning (980 rads)") != std::string::npos);

  DeathInput hazard;
  hazard.now = 1.0f;
  hazard.lastHit = HitNotes::LastHit{Hit(1, "Bear Trap", "", false), 0.5f};
  CHECK(reporter.Describe(hazard).find("an explosion, a trap or fire) (Bear Trap)") != std::string::npos);

  DeathInput fall;
  fall.healthFractionBefore = 0.9f;
  CHECK(reporter.Describe(fall).find("most likely a fall") != std::string::npos);

  DeathInput unknown;
  unknown.healthFractionBefore = 0.05f;
  CHECK(reporter.Describe(unknown).find("did not say") != std::string::npos);
}

TEST("death report: about one death in four is a roast, deterministic for a seed, never for hidden hazards") {
  int roasts = 0;
  const int total = 800;
  for (uint32_t seed = 1; seed <= total; ++seed) {
    DeathReporter reporter(seed * 2654435761u);
    DeathInput input = KilledBy("Viper");
    input.healthFractionAtFightStart = 0.1f;
    if (reporter.Describe(input).find("Bold strategy") != std::string::npos)
      ++roasts;
  }
  CHECK(roasts > total * 15 / 100 && roasts < total * 35 / 100);

  DeathReporter first(77);
  DeathReporter second(77);
  DeathInput input = KilledBy("Viper");
  for (int i = 0; i < 20; ++i)
    CHECK_EQ(first.Describe(input), second.Describe(input));

  DeathInput hazard;
  hazard.now = 1.0f;
  hazard.lastHit = HitNotes::LastHit{Hit(1, "Frag Mine", "", false), 0.5f};
  DeathInput fall;
  fall.healthFractionBefore = 0.9f;
  const std::string neutralHazard = DeathReporter(1).Describe(hazard);
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    DeathReporter reporter(seed);
    const std::string a = reporter.Describe(hazard);
    CHECK(a.substr(a.find("**Lesson:**")) == neutralHazard.substr(neutralHazard.find("**Lesson:**")));
    const std::string b = reporter.Describe(fall);
    CHECK(b.find("Bold") == std::string::npos && b.find("clearly") == std::string::npos);
  }
}

TEST("combat reporter: outside combat only health and radiation are reported") {
  CombatReporter reporter;
  PlayerSnapshot player;
  player.health = 100.0f;
  player.maxHealth = 100.0f;
  player.weaponName = "Pistol";
  player.weapon = WeaponKind::Ranged;
  player.usesAmmo = true;
  player.clip = 6;
  player.reserve = 20;
  reporter.Seed(player);

  player.weaponName = "Rifle"; // a weapon change out of combat is not news
  player.clip = 0;
  player.reserve = 0;
  CHECK(reporter.Update(player, false).empty());
  player.health = 40.0f; // a fall: banded health change is
  CHECK(reporter.Update(player, false).size() == 1);
  player.radiation = 450.0f;
  CHECK(reporter.Update(player, false).size() == 1);
  // The silently tracked weapon is not announced again when combat begins.
  CHECK(reporter.Update(player, true).empty());
}
