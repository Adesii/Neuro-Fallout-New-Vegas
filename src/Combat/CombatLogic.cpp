#include "CombatLogic.hpp"
#include "Actions/ActionData.hpp"
#include <algorithm>
#include <cmath>

namespace Combat {
namespace {

constexpr float kPulseOn = 0.10f;
constexpr float kRangedPulseOff = 0.16f;
constexpr float kMeleePulseOff = 0.40f;
constexpr float kThrownPulseOff = 0.90f;
constexpr float kLineOfSightLostLimit = 8.0f;
constexpr float kStallLimit = 20.0f;
constexpr float kDisabledLimit = 3.0f;
// Attacking this long without the target's health dropping means it cannot be hurt (essential, invulnerable, armoured).
constexpr float kNoEffectLimit = 15.0f;
constexpr float kDrawRetry = 1.5f;
constexpr float kReloadRetry = 2.0f;
constexpr int kFreeAimRangedPulses = 5;
constexpr int kFreeAimMeleePulses = 3;
constexpr int kFreeAimThrownPulses = 1;
constexpr float kFreeAimAutomaticSeconds = 1.5f;
constexpr float kRadiationStageSize = 200.0f;
constexpr int kMaxRadiationStage = 5;
constexpr float kHealthThresholds[] = {0.75f, 0.50f, 0.25f, 0.10f};
static_assert(sizeof(kHealthThresholds) / sizeof(kHealthThresholds[0]) == kBandCritical,
              "the band names must cover every threshold");
// A melee approach counts as progress while the distance shrinks by this much per window.
constexpr float kApproachWindow = 1.0f;
constexpr float kApproachMinGain = 20.0f;
// With the reserve unknown (the HUD text is missing), an empty magazine that stays empty this long means no ammunition.
constexpr float kEmptyClipUnknownReserveLimit = 6.0f;

float PulseOff(WeaponKind kind) {
  switch (kind) {
  case WeaponKind::Unarmed:
  case WeaponKind::Melee:
    return kMeleePulseOff;
  case WeaponKind::Thrown:
    return kThrownPulseOff;
  default:
    return kRangedPulseOff;
  }
}

int FreeAimPulseLimit(WeaponKind kind) {
  switch (kind) {
  case WeaponKind::Unarmed:
  case WeaponKind::Melee:
    return kFreeAimMeleePulses;
  case WeaponKind::Thrown:
    return kFreeAimThrownPulses;
  default:
    return kFreeAimRangedPulses;
  }
}

} // namespace

Disposition Classify(const DispositionInput &input) {
  const auto relation = static_cast<FactionRelation>(input.relation);
  if (input.fightingPlayer || relation == FactionRelation::Enemy)
    return Disposition::Hostile;
  if (input.teammate || relation == FactionRelation::Ally || relation == FactionRelation::Friend)
    return Disposition::Friendly;
  return input.aggression >= kVeryAggressive ? Disposition::Hostile : Disposition::Neutral;
}

const char *DispositionName(Disposition disposition) {
  switch (disposition) {
  case Disposition::Friendly:
    return "friendly";
  case Disposition::Hostile:
    return "hostile";
  default:
    return "neutral";
  }
}

WeaponKind ClassifyWeapon(int weaponType) {
  switch (weaponType) {
  case WeaponType::HandToHandMelee:
    return WeaponKind::Unarmed;
  case WeaponType::OneHandMelee:
  case WeaponType::TwoHandMelee:
    return WeaponKind::Melee;
  case WeaponType::OneHandGrenade:
  case WeaponType::OneHandThrown:
    return WeaponKind::Thrown;
  case WeaponType::OneHandMine:
  case WeaponType::OneHandLunchboxMine:
    return WeaponKind::Placed;
  default:
    return WeaponKind::Ranged;
  }
}

bool NeedsReach(WeaponKind kind) { return kind == WeaponKind::Unarmed || kind == WeaponKind::Melee; }

const char *WeaponKindName(WeaponKind kind) {
  switch (kind) {
  case WeaponKind::Unarmed:
    return "unarmed";
  case WeaponKind::Melee:
    return "melee";
  case WeaponKind::Thrown:
    return "thrown";
  case WeaponKind::Placed:
    return "placed explosive";
  default:
    return "ranged";
  }
}

std::string DescribeStop(StopReason reason, const std::string &target, bool soughtSight) {
  const std::string name = target.empty() ? "the target" : target;
  switch (reason) {
  case StopReason::Requested:
    return "You stopped attacking " + name + ".";
  case StopReason::TargetDead:
    return name + " is dead. You stopped attacking.";
  case StopReason::TargetGone:
    return "You stopped attacking " + name + " because it is no longer in the area.";
  case StopReason::ObjectFinished:
    return "You finished attacking " + name +
           ". Objects have no health to track, so look at the result yourself and use attack again to keep hitting it.";
  case StopReason::OutOfAmmo:
    return "You stopped attacking " + name +
           " because your weapon is out of ammunition. Equip another weapon or find more ammo.";
  case StopReason::NoEffect:
    return "You stopped attacking " + name +
           " because your attacks are not hurting it. It may be invulnerable or essential, or too heavily armored for "
           "this weapon.";
  case StopReason::LineOfSightLost:
    if (soughtSight)
      return "You stopped attacking " + name +
             " because you still have no clear view of it, and walking toward it did not open one. Try from another "
             "position, or pick another target.";
    return "You stopped attacking " + name +
           " because it has been out of your line of sight for too long (behind cover or a wall). Use attack with " +
           kApproachProperty + " set to true to walk toward it until you can see it, or move to a clear view yourself.";
  case StopReason::Stalled:
    return "You stopped attacking " + name + " because you could not make progress.";
  case StopReason::Unreachable:
    return "You stopped attacking " + name + " because you could not get to it.";
  case StopReason::ControlsDisabled:
    return "You stopped attacking " + name + " because your combat controls are disabled right now.";
  case StopReason::NoWeapon:
    return "You stopped attacking " + name + " because your equipped weapon cannot be used to attack.";
  case StopReason::PlayerDied:
    return "You stopped attacking " + name + " because you died.";
  case StopReason::AreaChanged:
    return "You stopped attacking " + name + " because you left the area.";
  }
  return "You stopped attacking " + name + ".";
}

void Engagement::Begin(bool targetIsActor, bool seekSight) {
  *this = Engagement();
  m_active = true;
  m_targetIsActor = targetIsActor;
  m_seekSight = seekSight;
}

EngagementOutput Engagement::Update(const EngagementInput &input) {
  EngagementOutput output;
  if (!m_active)
    return output;

  auto finish = [&](StopReason reason) {
    output.fire = false;
    output.stop = reason;
    m_active = false;
    m_triggerDown = false;
    return output;
  };

  if (!input.targetPresent)
    return finish(StopReason::TargetGone);
  if (!input.targetAlive)
    return finish(StopReason::TargetDead);
  if (input.weapon == WeaponKind::Placed)
    return finish(StopReason::NoWeapon);
  // The walk toward the target gave up (no path, blocked, stuck). Walker has been through its own recovery by then,
  // so starting the same walk again would only fail again, with a failure message each time.
  if (input.approachFailed)
    return finish(StopReason::Unreachable);

  // A menu or VATS owns the player. The engagement survives it, but nothing is pressed and no timer runs.
  if (input.paused) {
    m_triggerDown = false;
    m_pulseTimer = kPulseReady;
    m_blocker = "a menu is open";
    return output;
  }

  output.aim = true;
  m_drawCooldown = std::max(0.0f, m_drawCooldown - input.dt);
  m_reloadCooldown = std::max(0.0f, m_reloadCooldown - input.dt);

  if (input.fightDisabled) {
    m_triggerDown = false;
    m_blocker = "the fight control is disabled";
    m_disabledTime += input.dt;
    if (m_disabledTime >= kDisabledLimit)
      return finish(StopReason::ControlsDisabled);
    return output;
  }
  m_disabledTime = 0.0f;

  if (input.usesAmmo && input.clip == 0 && input.reserve == 0)
    return finish(StopReason::OutOfAmmo);
  // The reserve is read from the HUD and can be missing; a magazine that never refills is then the only sign.
  if (input.usesAmmo && input.clip == 0 && input.reserve < 0) {
    m_emptyClipTime += input.dt;
    if (m_emptyClipTime >= kEmptyClipUnknownReserveLimit)
      return finish(StopReason::OutOfAmmo);
  } else {
    m_emptyClipTime = 0.0f;
  }

  bool canFire = true;
  m_blocker = "";
  if (input.weapon != WeaponKind::Unarmed && !input.weaponDrawn) {
    canFire = false;
    m_blocker = "the weapon is not drawn";
    if (m_drawCooldown <= 0.0f) {
      output.tapReadyWeapon = true;
      m_drawCooldown = kDrawRetry;
    }
  } else if (input.usesAmmo && input.clip == 0) {
    canFire = false;
    m_blocker = "the weapon is reloading";
    if (m_reloadCooldown <= 0.0f) {
      output.tapReload = true;
      m_reloadCooldown = kReloadRetry;
    }
  }

  // Why she should be walking toward the target, if at all: for a view of it, or to get within melee reach.
  enum class Walk { None, Sight, Reach };
  Walk walk = Walk::None;
  if (!input.lineOfSight) {
    canFire = false;
    m_blocker = "there is no line of sight";
    m_sightTime = 0.0f;
    if (m_seekSight) {
      m_seekingSight = true;
      walk = Walk::Sight;
    }
  } else {
    m_losLostTime = 0.0f;
    m_sightTime += input.dt;
    if (m_seekingSight && m_sightTime < kSightHold)
      walk = Walk::Sight;
    else
      m_seekingSight = false;
  }
  if (NeedsReach(input.weapon) && input.distance > input.meleeRange) {
    canFire = false;
    m_blocker = "the target is out of melee reach";
    if (walk == Walk::None)
      walk = Walk::Reach;
  }
  output.approach = walk != Walk::None && !input.externalMovement;
  const bool approaching = output.approach;

  // An approach is progress, not a stall, while it gets somewhere. Toward melee reach the straight distance has to
  // shrink. The way to a view of the target leads around whatever hides it, which often does not shorten the straight
  // line, so there the ground covered counts: a walk that stands still (arrived, or pushing into something) does not.
  if (approaching) {
    const bool bySight = walk == Walk::Sight;
    const float measure = bySight ? input.travelled : -input.distance;
    if (!m_approachMeasured || bySight != m_approachBySight) {
      m_approachMeasured = true;
      m_approachBySight = bySight;
      m_approachMark = measure;
      m_approachTime = 0.0f;
    }
    m_approachTime += input.dt;
    if (m_approachTime >= kApproachWindow) {
      m_approachProgressing = measure - m_approachMark >= kApproachMinGain;
      m_approachMark = measure;
      m_approachTime = 0.0f;
    }
  } else {
    m_approachMeasured = false;
    m_approachTime = 0.0f;
    m_approachProgressing = true;
  }

  // The line-of-sight limit counts the time spent without a view and without doing anything about it. Walking toward
  // the target for a view is doing something about it, as long as the walk is getting somewhere.
  if (!input.lineOfSight) {
    if (approaching && walk == Walk::Sight && m_approachProgressing) {
      m_losLostTime = 0.0f;
    } else {
      m_losLostTime += input.dt;
      if (m_losLostTime >= kLineOfSightLostLimit)
        return finish(StopReason::LineOfSightLost);
    }
  }
  if (!input.aimed && canFire) {
    canFire = false;
    m_blocker = "the shot is not lined up yet";
  }

  // Progress watchdog: a live, reachable target that never loses health is not going to die from this.
  if (m_targetIsActor && input.targetHealth >= 0.0f) {
    if (m_lastTargetHealth >= 0.0f && input.targetHealth < m_lastTargetHealth - 0.01f)
      m_ineffectiveTime = 0.0f;
    m_lastTargetHealth = input.targetHealth;
    if (canFire) {
      m_ineffectiveTime += input.dt;
      if (m_ineffectiveTime >= kNoEffectLimit)
        return finish(StopReason::NoEffect);
    }
  }

  if (canFire || (approaching && m_approachProgressing)) {
    m_stallTime = 0.0f;
  } else if (!input.externalMovement) {
    m_stallTime += input.dt;
    if (m_stallTime >= kStallLimit)
      return finish(StopReason::Stalled);
  }

  if (!canFire) {
    m_triggerDown = false;
    m_pulseTimer = kPulseReady;
    return output;
  }

  if (input.automatic && input.weapon == WeaponKind::Ranged) {
    m_triggerDown = true;
    if (!m_targetIsActor) {
      m_freeAimTime += input.dt;
      if (m_freeAimTime >= kFreeAimAutomaticSeconds)
        return finish(StopReason::ObjectFinished);
    }
  } else {
    m_pulseTimer += input.dt;
    if (m_triggerDown) {
      if (m_pulseTimer >= kPulseOn) {
        m_triggerDown = false;
        m_pulseTimer = 0.0f;
        ++m_pulses;
        if (!m_targetIsActor && m_pulses >= FreeAimPulseLimit(input.weapon))
          return finish(StopReason::ObjectFinished);
      }
    } else if (m_pulseTimer >= PulseOff(input.weapon)) {
      m_triggerDown = true;
      m_pulseTimer = 0.0f;
    }
  }
  output.fire = m_triggerDown;
  return output;
}

void AimGate::Reset() {
  m_lastError = 0.0f;
  m_still = 0.0f;
  m_hasError = false;
}

bool AimGate::Update(float dt, float headingError, float pitchError, float headingTolerance, float pitchTolerance) {
  dt = std::isfinite(dt) ? std::max(dt, 0.0f) : 0.0f;
  if (!std::isfinite(headingError) || !std::isfinite(pitchError)) {
    Reset();
    return false;
  }
  const float error = std::abs(headingError) + std::abs(pitchError);
  if (dt > 0.0f) {
    const bool still = m_hasError && std::abs(error - m_lastError) <= kSettleRate * dt;
    m_still = still ? m_still + dt : 0.0f;
    m_lastError = error;
    m_hasError = true;
  }
  const bool within = std::abs(headingError) <= headingTolerance && std::abs(pitchError) <= pitchTolerance;
  return within && m_still >= kSettleSeconds;
}

float AngularRadius(float radius, float distance, float fallback) {
  if (!std::isfinite(radius) || !std::isfinite(distance) || radius <= 0.0f || distance <= 0.0f)
    return fallback;
  return std::min(std::atan2(radius, distance), fallback);
}

int HealthBand(float health, float maxHealth) {
  const float fraction = maxHealth > 0.0f ? std::clamp(health / maxHealth, 0.0f, 1.0f) : 1.0f;
  int band = 0;
  for (float threshold : kHealthThresholds) {
    if (fraction < threshold)
      ++band;
  }
  return band;
}

int RadiationStage(float radiation) {
  if (!(radiation > 0.0f))
    return 0;
  return std::min(static_cast<int>(radiation / kRadiationStageSize), kMaxRadiationStage);
}

std::string FormatFraction(float value, float maximum) {
  const int current = static_cast<int>(std::lround(value));
  const int total = static_cast<int>(std::lround(maximum));
  const int percent =
      maximum > 0.0f ? static_cast<int>(std::lround(std::clamp(value / maximum, 0.0f, 1.0f) * 100.0f)) : 0;
  return std::to_string(current) + "/" + std::to_string(total) + " (" + std::to_string(percent) + "%)";
}

void CombatReporter::Reset() { *this = CombatReporter(); }

void CombatReporter::Seed(const PlayerSnapshot &snapshot) {
  m_seeded = true;
  m_healthBand = HealthBand(snapshot.health, snapshot.maxHealth);
  m_radStage = RadiationStage(snapshot.radiation);
  m_weaponName = snapshot.weaponName;
  m_clipEmptyReported = snapshot.usesAmmo && snapshot.clip == 0;
  m_outOfAmmoReported = snapshot.usesAmmo && snapshot.clip == 0 && snapshot.reserve == 0;
}

std::vector<std::string> CombatReporter::Update(const PlayerSnapshot &snapshot, bool inCombat) {
  std::vector<std::string> notes;
  if (!m_seeded) {
    Seed(snapshot);
    return notes;
  }

  const int band = HealthBand(snapshot.health, snapshot.maxHealth);
  if (band > m_healthBand) {
    const char *severity = band >= kBandCritical  ? "critically low"
                           : band >= kBandVeryLow ? "very low"
                           : band >= kBandLow     ? "low"
                                                  : "dropping";
    notes.push_back(std::string("Your health is ") + severity + ": " +
                    FormatFraction(snapshot.health, snapshot.maxHealth) + ".");
  }
  m_healthBand = band;

  const int stage = RadiationStage(snapshot.radiation);
  if (stage > m_radStage)
    notes.push_back("Your radiation exposure rose to " + std::to_string(static_cast<int>(snapshot.radiation)) +
                    " rads (stage " + std::to_string(stage) + " of " + std::to_string(kMaxRadiationStage) + ").");
  m_radStage = stage;

  if (snapshot.weaponName != m_weaponName) {
    m_weaponName = snapshot.weaponName;
    m_clipEmptyReported = false;
    m_outOfAmmoReported = false;
    if (inCombat)
      notes.push_back("You are now using " +
                      (snapshot.weaponName.empty() ? std::string("no weapon") : snapshot.weaponName) + " (" +
                      WeaponKindName(snapshot.weapon) + ").");
  }

  if (snapshot.usesAmmo) {
    const bool noAmmoLeft = snapshot.clip == 0 && snapshot.reserve == 0;
    const bool clipEmpty = snapshot.clip == 0 && snapshot.reserve != 0;
    if (noAmmoLeft) {
      if (!m_outOfAmmoReported && inCombat)
        notes.push_back("You have no ammunition left for " + snapshot.weaponName + ".");
    } else if (clipEmpty && !m_clipEmptyReported && inCombat) {
      std::string note = "The magazine of " + snapshot.weaponName + " is empty";
      if (snapshot.reserve > 0)
        note += " (" + std::to_string(snapshot.reserve) + " rounds in reserve)";
      notes.push_back(note + ".");
    }
    m_outOfAmmoReported = noAmmoLeft;
    m_clipEmptyReported = snapshot.clip == 0;
  }
  return notes;
}

bool ParseAttackRequest(std::string_view json, AttackRequest &request, std::string &error) {
  auto data = Actions::ActionData::Parse(json, error);
  if (!data)
    return false;
  const bool hasEnemy = data->IsObject() && data->HasProperty("enemy_id");
  const bool hasObject = data->IsObject() && data->HasProperty("object_id");
  const bool hasApproach = data->IsObject() && data->HasProperty(kApproachProperty);
  if (!data->IsObject() || hasEnemy == hasObject || data->Size() != (hasApproach ? 2u : 1u)) {
    error = "Provide exactly one of enemy_id (from query_enemies) or object_id (from query_nearby or search_nearby), "
            "and optionally approach (true or false).";
    return false;
  }
  int id = 0;
  if (!data->GetPositiveInteger(hasEnemy ? "enemy_id" : "object_id", id, error))
    return false;
  bool approach = false;
  if (hasApproach && data->ReadBoolean(kApproachProperty, approach) != Actions::ActionData::BooleanResult::Ok) {
    error = std::string(kApproachProperty) + " must be true or false.";
    return false;
  }
  request = {};
  (hasEnemy ? request.enemyId : request.objectId) = id;
  request.approach = approach;
  return true;
}

bool ParseOptionalEnemyId(std::string_view json, std::optional<int> &enemyId, std::string &error) {
  enemyId.reset();
  if (json.empty())
    return true;
  auto data = Actions::ActionData::Parse(json, error);
  if (!data)
    return false;
  if (!data->IsObject() || data->Size() > 1 || (data->Size() == 1 && !data->HasProperty("enemy_id"))) {
    error = "The only supported property is enemy_id (from query_enemies).";
    return false;
  }
  if (data->Size() == 0)
    return true;
  int id = 0;
  if (!data->GetPositiveInteger("enemy_id", id, error))
    return false;
  enemyId = id;
  return true;
}

} // namespace Combat
