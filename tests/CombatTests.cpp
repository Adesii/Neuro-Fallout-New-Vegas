#include "Combat/CombatLogic.hpp"
#include "TestFramework.hpp"

using namespace Combat;

namespace {

EngagementInput ReadyRanged() {
  EngagementInput input;
  input.dt = 0.05f;
  input.weapon = WeaponKind::Ranged;
  input.weaponDrawn = true;
  input.lineOfSight = true;
  input.aimed = true;
  input.usesAmmo = true;
  input.clip = 10;
  input.reserve = 50;
  return input;
}

// Runs the engagement for `seconds` and reports how many frames had the trigger down.
int FramesFiring(Engagement &engagement, EngagementInput input, float seconds) {
  int firing = 0;
  for (float elapsed = 0.0f; elapsed < seconds && engagement.IsActive(); elapsed += input.dt) {
    if (engagement.Update(input).fire)
      ++firing;
  }
  return firing;
}

} // namespace

TEST("combat: only explicit ally/friend/teammate are friendly, neutrals stay attackable") {
  CHECK(Classify({.relation = 3}) == Disposition::Friendly);
  CHECK(Classify({.relation = 2}) == Disposition::Friendly);
  CHECK(Classify({.relation = 0, .teammate = true}) == Disposition::Friendly);
  CHECK(Classify({.relation = 0}) == Disposition::Neutral);
  CHECK(Classify({.relation = 1}) == Disposition::Hostile);
}

TEST("combat: an actor fighting the player is hostile whatever its faction relation") {
  CHECK(Classify({.relation = 3, .fightingPlayer = true}) == Disposition::Hostile);
  CHECK(Classify({.relation = 0, .teammate = true, .fightingPlayer = true}) == Disposition::Hostile);
  CHECK(Classify({.relation = 0, .fightingPlayer = true}) == Disposition::Hostile);
}

TEST("combat: a very aggressive neutral is labelled hostile, but friendliness always wins") {
  CHECK(Classify({.relation = 0, .aggression = 2}) == Disposition::Hostile);
  CHECK(Classify({.relation = 0, .aggression = 3}) == Disposition::Hostile);
  CHECK(Classify({.relation = 0, .aggression = 1}) == Disposition::Neutral);
  CHECK(Classify({.relation = 0, .aggression = 0}) == Disposition::Neutral);
  CHECK(Classify({.relation = 3, .aggression = 3}) == Disposition::Friendly);
  CHECK(Classify({.relation = 2, .aggression = 3}) == Disposition::Friendly);
  CHECK(Classify({.relation = 0, .teammate = true, .aggression = 3}) == Disposition::Friendly);
}

TEST("combat: weapon types group by how the attack is performed") {
  CHECK(ClassifyWeapon(0) == WeaponKind::Unarmed);
  CHECK(ClassifyWeapon(1) == WeaponKind::Melee);
  CHECK(ClassifyWeapon(2) == WeaponKind::Melee);
  for (int type : {3, 4, 5, 6, 7, 8, 9})
    CHECK(ClassifyWeapon(type) == WeaponKind::Ranged);
  CHECK(ClassifyWeapon(10) == WeaponKind::Thrown);
  CHECK(ClassifyWeapon(13) == WeaponKind::Thrown);
  CHECK(ClassifyWeapon(11) == WeaponKind::Placed);
  CHECK(ClassifyWeapon(12) == WeaponKind::Placed);
  CHECK(NeedsReach(WeaponKind::Unarmed) && NeedsReach(WeaponKind::Melee));
  CHECK(!NeedsReach(WeaponKind::Ranged) && !NeedsReach(WeaponKind::Thrown));
}

TEST("combat: attack arguments need exactly one positive id") {
  AttackRequest request;
  std::string error;
  CHECK(ParseAttackRequest(R"({"enemy_id":3})", request, error));
  CHECK(request.enemyId == std::optional<int>(3) && !request.objectId);
  CHECK(ParseAttackRequest(R"({"object_id":7})", request, error));
  CHECK(request.objectId == std::optional<int>(7) && !request.enemyId);

  CHECK(!ParseAttackRequest("", request, error));
  CHECK(!ParseAttackRequest("{}", request, error));
  CHECK(!ParseAttackRequest(R"({"enemy_id":1,"object_id":2})", request, error));
  CHECK(!ParseAttackRequest(R"({"enemy_id":0})", request, error));
  CHECK(!ParseAttackRequest(R"({"enemy_id":"a"})", request, error));
  CHECK(!ParseAttackRequest(R"({"target":1})", request, error));
  CHECK(!ParseAttackRequest("[1]", request, error));
}

TEST("combat: open_vats accepts an optional enemy id only") {
  std::optional<int> enemy;
  std::string error;
  CHECK(ParseOptionalEnemyId("", enemy, error) && !enemy);
  CHECK(ParseOptionalEnemyId("{}", enemy, error) && !enemy);
  CHECK(ParseOptionalEnemyId(R"({"enemy_id":2})", enemy, error) && enemy == std::optional<int>(2));
  CHECK(!ParseOptionalEnemyId(R"({"enemy_id":-1})", enemy, error));
  CHECK(!ParseOptionalEnemyId(R"({"other":1})", enemy, error));
}

TEST("combat engagement: an automatic ranged weapon holds the trigger while it can see and aim at the target") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.automatic = true;
  const auto output = engagement.Update(input);
  CHECK(output.aim && output.fire && !output.stop);
}

TEST("combat engagement: a semi-automatic weapon pulses the trigger instead of holding it") {
  Engagement engagement;
  engagement.Begin(true);
  const int total = static_cast<int>(2.0f / 0.05f);
  const int firing = FramesFiring(engagement, ReadyRanged(), 2.0f);
  CHECK(firing > 0);
  CHECK(firing < total / 2);
}

TEST("combat engagement: nothing fires without line of sight and the engagement ends after the limit") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.lineOfSight = false;
  EngagementOutput last;
  for (int frame = 0; frame < 400 && engagement.IsActive(); ++frame) {
    last = engagement.Update(input);
    CHECK(!last.fire);
  }
  CHECK(last.stop == std::optional<StopReason>(StopReason::LineOfSightLost));
}

TEST("combat engagement: peeking out of cover resets the line-of-sight timer") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.automatic = true;
  for (int round = 0; round < 5; ++round) {
    input.lineOfSight = false;
    for (int frame = 0; frame < 100; ++frame) // 5 s hidden, under the limit
      CHECK(!engagement.Update(input).stop);
    input.lineOfSight = true;
    CHECK(engagement.Update(input).fire);
  }
  CHECK(engagement.IsActive());
}

TEST("combat engagement: melee approaches when out of reach and swings only inside it") {
  Engagement engagement;
  engagement.Begin(true);
  EngagementInput input;
  input.dt = 0.05f;
  input.weapon = WeaponKind::Melee;
  input.weaponDrawn = true;
  input.lineOfSight = true;
  input.aimed = true;
  input.meleeRange = 100.0f;

  input.distance = 400.0f;
  auto output = engagement.Update(input);
  CHECK(output.approach && !output.fire);

  input.externalMovement = true; // Neuro is walking: her move wins, no competing approach
  CHECK(!engagement.Update(input).approach);
  input.externalMovement = false;

  input.distance = 80.0f;
  CHECK(FramesFiring(engagement, input, 2.0f) > 0);
}

TEST("combat engagement: ranged weapons ignore distance") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.automatic = true;
  input.distance = 5000.0f;
  const auto output = engagement.Update(input);
  CHECK(output.fire && !output.approach);
}

TEST("combat engagement: a holstered weapon is drawn first, then retried only after the cooldown") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.weaponDrawn = false;
  auto output = engagement.Update(input);
  CHECK(output.tapReadyWeapon && !output.fire);
  CHECK(!engagement.Update(input).tapReadyWeapon);
  input.dt = 2.0f;
  CHECK(engagement.Update(input).tapReadyWeapon);
}

TEST("combat engagement: unarmed attacks do not need a drawn weapon") {
  Engagement engagement;
  engagement.Begin(true);
  EngagementInput input;
  input.dt = 0.05f;
  input.weapon = WeaponKind::Unarmed;
  input.weaponDrawn = false;
  input.lineOfSight = true;
  input.aimed = true;
  input.distance = 50.0f;
  const auto output = engagement.Update(input);
  CHECK(!output.tapReadyWeapon);
  CHECK(FramesFiring(engagement, input, 2.0f) > 0);
}

TEST("combat engagement: an empty magazine reloads, an empty inventory ends the fight") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.clip = 0;
  auto output = engagement.Update(input);
  CHECK(output.tapReload && !output.fire && !output.stop);

  input.reserve = 0;
  output = engagement.Update(input);
  CHECK(output.stop == std::optional<StopReason>(StopReason::OutOfAmmo));
  CHECK(!engagement.IsActive());
}

TEST("combat engagement: a dead or vanished target ends the fight with the right reason") {
  Engagement dead;
  dead.Begin(true);
  auto input = ReadyRanged();
  input.targetAlive = false;
  CHECK(dead.Update(input).stop == std::optional<StopReason>(StopReason::TargetDead));

  Engagement gone;
  gone.Begin(true);
  input = ReadyRanged();
  input.targetPresent = false;
  CHECK(gone.Update(input).stop == std::optional<StopReason>(StopReason::TargetGone));
}

TEST("combat engagement: a menu pauses without ending, releases the trigger and freezes the timers") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.automatic = true;
  CHECK(engagement.Update(input).fire);

  input.paused = true;
  input.lineOfSight = false;
  for (int frame = 0; frame < 1000; ++frame) {
    const auto output = engagement.Update(input);
    CHECK(!output.fire && !output.aim && !output.stop);
  }
  CHECK(engagement.IsActive());

  input.paused = false;
  input.lineOfSight = true;
  CHECK(engagement.Update(input).fire);
}

TEST("combat engagement: disabled fight controls end the fight only after a grace period") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.fightDisabled = true;
  EngagementOutput output;
  int frames = 0;
  while (engagement.IsActive() && frames < 1000) {
    output = engagement.Update(input);
    CHECK(!output.fire);
    ++frames;
  }
  CHECK(output.stop == std::optional<StopReason>(StopReason::ControlsDisabled));
  CHECK(frames > 10);
}

TEST("combat engagement: an unaimed shot waits, and a long stall ends the fight") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.aimed = false;
  EngagementOutput output;
  for (int frame = 0; frame < 2000 && engagement.IsActive(); ++frame) {
    output = engagement.Update(input);
    CHECK(!output.fire);
  }
  CHECK(output.stop == std::optional<StopReason>(StopReason::Stalled));
}

TEST("combat engagement: Neuro walking does not count as a stall") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.aimed = false;
  input.externalMovement = true;
  for (int frame = 0; frame < 2000; ++frame)
    CHECK(!engagement.Update(input).stop);
}

TEST("combat engagement: free-aim targets finish after a bounded burst") {
  Engagement semi;
  semi.Begin(false);
  EngagementOutput last;
  for (int frame = 0; frame < 400 && semi.IsActive(); ++frame)
    last = semi.Update(ReadyRanged());
  CHECK(last.stop == std::optional<StopReason>(StopReason::ObjectFinished));

  Engagement automatic;
  automatic.Begin(false);
  auto input = ReadyRanged();
  input.automatic = true;
  for (int frame = 0; frame < 400 && automatic.IsActive(); ++frame)
    last = automatic.Update(input);
  CHECK(last.stop == std::optional<StopReason>(StopReason::ObjectFinished));
}

TEST("combat engagement: attacking a target whose health never drops ends with a no-effect reason") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.automatic = true;
  input.targetHealth = 50.0f;
  EngagementOutput last;
  float seconds = 0.0f;
  while (engagement.IsActive() && seconds < 60.0f) {
    last = engagement.Update(input);
    seconds += input.dt;
  }
  CHECK(last.stop == std::optional<StopReason>(StopReason::NoEffect));
  CHECK(seconds > 14.0f && seconds < 17.0f);
}

TEST("combat engagement: any damage restarts the no-effect clock") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.automatic = true;
  input.targetHealth = 100.0f;
  for (int round = 0; round < 6; ++round) { // 6 x 10 s of firing, but a hit lands every round
    for (int frame = 0; frame < 200; ++frame)
      CHECK(!engagement.Update(input).stop);
    input.targetHealth -= 5.0f;
  }
  CHECK(engagement.IsActive());
}

TEST("combat engagement: no-effect needs known health and a real actor, and ignores time spent unable to fire") {
  Engagement unknown;
  unknown.Begin(true);
  auto input = ReadyRanged();
  input.automatic = true;
  for (int frame = 0; frame < 2000; ++frame)
    CHECK(!unknown.Update(input).stop); // health unknown: -1

  Engagement object;
  object.Begin(false);
  input.targetHealth = 10.0f;
  EngagementOutput last;
  for (int frame = 0; frame < 2000 && object.IsActive(); ++frame)
    last = object.Update(input);
  CHECK(last.stop != std::optional<StopReason>(StopReason::NoEffect)); // free-aim ends by its own burst limit

  Engagement hiding;
  hiding.Begin(true);
  input.lineOfSight = false;
  for (int frame = 0; frame < 120; ++frame) // 6 s behind cover is not 15 s of ineffective firing
    CHECK(!hiding.Update(input).stop);
  input.lineOfSight = true;
  for (int frame = 0; frame < 200; ++frame) // another 10 s in the open: still under the limit
    CHECK(!hiding.Update(input).stop);
}

TEST("combat engagement: placed explosives cannot be used to attack") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.weapon = WeaponKind::Placed;
  CHECK(engagement.Update(input).stop == std::optional<StopReason>(StopReason::NoWeapon));
}

TEST("combat engagement: retargeting restarts cleanly") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.lineOfSight = false;
  for (int frame = 0; frame < 150; ++frame)
    engagement.Update(input);
  engagement.Begin(true); // a new attack call replaces the target and its timers
  for (int frame = 0; frame < 150; ++frame)
    CHECK(!engagement.Update(input).stop);
}

TEST("combat reporter: health is announced when it crosses a lower band, not on every point lost") {
  CombatReporter reporter;
  PlayerSnapshot player;
  player.health = 100;
  player.maxHealth = 100;
  player.weaponName = "Pistol";
  reporter.Seed(player);

  player.health = 90;
  CHECK(reporter.Update(player).empty());
  player.health = 70; // below 75%
  CHECK(reporter.Update(player).size() == 1);
  player.health = 65;
  CHECK(reporter.Update(player).empty());
  player.health = 20; // jumps two bands: still a single note
  CHECK(reporter.Update(player).size() == 1);
  player.health = 100; // recovery is silent but re-arms the bands
  CHECK(reporter.Update(player).empty());
  player.health = 40;
  CHECK(reporter.Update(player).size() == 1);
}

TEST("combat reporter: radiation is announced per 200-rad stage") {
  CombatReporter reporter;
  PlayerSnapshot player;
  player.health = player.maxHealth = 100;
  reporter.Seed(player);
  player.radiation = 150;
  CHECK(reporter.Update(player).empty());
  player.radiation = 210;
  CHECK(reporter.Update(player).size() == 1);
  player.radiation = 390;
  CHECK(reporter.Update(player).empty());
  player.radiation = 420;
  CHECK(reporter.Update(player).size() == 1);
}

TEST("combat reporter: a weapon change and ammo exhaustion are each reported once") {
  CombatReporter reporter;
  PlayerSnapshot player;
  player.health = player.maxHealth = 100;
  player.weaponName = "Pistol";
  player.usesAmmo = true;
  player.clip = 3;
  player.reserve = 12;
  reporter.Seed(player);

  player.clip = 0;
  auto notes = reporter.Update(player);
  CHECK(notes.size() == 1);
  CHECK(notes[0].find("empty") != std::string::npos);
  CHECK(reporter.Update(player).empty()); // not repeated every frame

  player.clip = 8;
  player.reserve = 4;
  CHECK(reporter.Update(player).empty());
  player.clip = 0;
  player.reserve = 0;
  notes = reporter.Update(player);
  CHECK(notes.size() == 1);
  CHECK(notes[0].find("no ammunition") != std::string::npos);
  CHECK(reporter.Update(player).empty());

  player.weaponName = "Combat Knife";
  player.weapon = WeaponKind::Melee;
  player.usesAmmo = false;
  notes = reporter.Update(player);
  CHECK(notes.size() == 1);
  CHECK(notes[0].find("Combat Knife") != std::string::npos);
}

TEST("combat: stop messages always name what was being attacked") {
  for (auto reason :
       {StopReason::Requested, StopReason::TargetDead, StopReason::TargetGone, StopReason::ObjectFinished,
        StopReason::OutOfAmmo, StopReason::NoEffect, StopReason::LineOfSightLost, StopReason::Stalled,
        StopReason::ControlsDisabled, StopReason::NoWeapon, StopReason::PlayerDied, StopReason::AreaChanged}) {
    CHECK(DescribeStop(reason, "Powder Ganger").find("Powder Ganger") != std::string::npos);
  }
}

TEST("combat: health bands and fractions format as expected") {
  CHECK(HealthBand(100, 100) == 0);
  CHECK(HealthBand(74, 100) == 1);
  CHECK(HealthBand(49, 100) == 2);
  CHECK(HealthBand(24, 100) == 3);
  CHECK(HealthBand(9, 100) == 4);
  CHECK(HealthBand(0, 0) == 0);
  CHECK(FormatFraction(48, 120) == "48/120 (40%)");
  CHECK(RadiationStage(0) == 0 && RadiationStage(199) == 0 && RadiationStage(200) == 1 && RadiationStage(5000) == 5);
}

TEST("combat engagement: the first shot of a semi-automatic weapon is not delayed by the pulse off time") {
  Engagement engagement;
  engagement.Begin(true);
  CHECK(engagement.Update(ReadyRanged()).fire);
}

TEST("combat engagement: the trigger goes down on the very frame the weapon first becomes ready") {
  for (auto kind : {WeaponKind::Ranged, WeaponKind::Thrown, WeaponKind::Melee}) {
    Engagement engagement;
    engagement.Begin(true);
    auto input = ReadyRanged();
    input.weapon = kind;
    input.meleeRange = 100.0f;
    input.distance = 50.0f;
    input.aimed = false;
    for (int frame = 0; frame < 30; ++frame)
      CHECK(!engagement.Update(input).fire); // lining up the shot
    input.aimed = true;
    CHECK(engagement.Update(input).fire);
  }
}

TEST("combat engagement: after a menu closes the first shot is immediate again") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  CHECK(engagement.Update(input).fire);
  input.paused = true;
  CHECK(!engagement.Update(input).fire);
  input.paused = false;
  CHECK(engagement.Update(input).fire);
}

TEST("combat engagement: a pulsed weapon still waits the off time between shots") {
  Engagement engagement;
  engagement.Begin(true);
  const auto input = ReadyRanged();
  CHECK(engagement.Update(input).fire);
  int frames = 0;
  while (engagement.Update(input).fire && frames < 100)
    ++frames; // the trigger comes up
  int waiting = 0;
  while (!engagement.Update(input).fire && waiting < 100)
    ++waiting;
  CHECK(waiting >= 2); // 0.16 s off time at 0.05 s per frame
}

namespace {
EngagementInput MeleeInput(float distance) {
  EngagementInput input;
  input.dt = 0.1f;
  input.weapon = WeaponKind::Melee;
  input.weaponDrawn = true;
  input.lineOfSight = true;
  input.aimed = true;
  input.meleeRange = 100.0f;
  input.distance = distance;
  return input;
}
} // namespace

TEST("combat engagement: a long melee approach that keeps closing in never counts as a stall") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = MeleeInput(30000.0f);
  for (int frame = 0; frame < 2000 && input.distance > 100.0f; ++frame) { // 300 units per second for 100 s
    input.distance -= 30.0f;
    const auto output = engagement.Update(input);
    CHECK(!output.stop);
    if (output.stop)
      break;
  }
  CHECK(engagement.IsActive());
}

TEST("combat engagement: an approach that does not get any closer still stalls after the limit") {
  Engagement engagement;
  engagement.Begin(true);
  const auto input = MeleeInput(3000.0f);
  EngagementOutput last;
  float elapsed = 0.0f;
  while (engagement.IsActive() && elapsed < 40.0f) {
    last = engagement.Update(input);
    elapsed += input.dt;
  }
  CHECK(last.stop == std::optional<StopReason>(StopReason::Stalled));
  CHECK(elapsed > 19.0f && elapsed < 25.0f);
}

TEST("combat engagement: an empty magazine with an unknown reserve ends as out of ammo, not as a stall") {
  Engagement engagement;
  engagement.Begin(true);
  auto input = ReadyRanged();
  input.clip = 0;
  input.reserve = -1;
  EngagementOutput last;
  float elapsed = 0.0f;
  while (engagement.IsActive() && elapsed < 15.0f) {
    last = engagement.Update(input);
    elapsed += input.dt;
  }
  CHECK(last.stop == std::optional<StopReason>(StopReason::OutOfAmmo));
  CHECK(elapsed < 8.0f);
}

TEST("combat engagement: a reload that fills the magazine, or a known reserve, never ends it as out of ammo") {
  Engagement refilled;
  refilled.Begin(true);
  auto input = ReadyRanged();
  input.reserve = -1;
  for (int round = 0; round < 10; ++round) {
    input.clip = 0;
    for (int frame = 0; frame < 80; ++frame) // 4 s reloading
      CHECK(!refilled.Update(input).stop);
    input.clip = 10;
    refilled.Update(input);
  }

  Engagement reserve;
  reserve.Begin(true);
  input = ReadyRanged();
  input.clip = 0;
  input.reserve = 30;
  for (int frame = 0; frame < 100; ++frame)
    CHECK(!reserve.Update(input).stop);
}

TEST("combat: weapon type constants and health band names line up with their tables") {
  CHECK(ClassifyWeapon(WeaponType::HandToHandMelee) == WeaponKind::Unarmed);
  CHECK(ClassifyWeapon(WeaponType::OneHandMelee) == WeaponKind::Melee);
  CHECK(ClassifyWeapon(WeaponType::TwoHandLauncher) == WeaponKind::Ranged);
  CHECK(ClassifyWeapon(WeaponType::OneHandGrenade) == WeaponKind::Thrown);
  CHECK(ClassifyWeapon(WeaponType::OneHandLunchboxMine) == WeaponKind::Placed);
  CHECK(HealthBand(5.0f, 100.0f) == kBandCritical);
  CHECK(HealthBand(20.0f, 100.0f) == kBandVeryLow);
  CHECK(HealthBand(40.0f, 100.0f) == kBandLow);
  CHECK(HealthBand(70.0f, 100.0f) == kBandDropping);
  CHECK(HealthBand(100.0f, 100.0f) == 0);
}
