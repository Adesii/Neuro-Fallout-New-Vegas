#pragma once

// Engine-independent combat rules: hostility classification, weapon classification, the attack engagement state
// machine, combat context notes and `attack` argument parsing. Nothing here reads game memory, so the host tests
// can cover it. CombatHandler feeds it engine observations and applies its outputs.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Combat {

// Reaction ranks returned by the engine's actor-to-actor faction relation (FalloutNV.exe 1.4.0.525 0x8B87A0, whose
// result combiner at 0x8B8740 orders them this way; the same call site in Actor::AttackedBy appears in itr-nvse).
enum class FactionRelation { Neutral = 0, Enemy = 1, Ally = 2, Friend = 3 };

enum class Disposition { Friendly, Neutral, Hostile };

// Aggression actor value scale (GECK dropdown order): 0 Unaggressive, 1 Aggressive, 2 Very Aggressive, 3 Frenzied.
inline constexpr int kVeryAggressive = 2;

struct DispositionInput {
  int relation = 0; // FactionRelation as the engine reports it
  bool teammate = false;
  bool fightingPlayer = false; // the actor is currently in combat against the player
  int aggression = 0;
};

// Only explicitly friendly actors (ally/friend relation or a teammate) are Friendly, and only while they are not
// fighting the player. Plain neutrals stay attackable: killing one is Neuro's choice. A neutral that is very
// aggressive attacks on sight, so it is labelled Hostile; that changes its label and order, never its eligibility.
Disposition Classify(const DispositionInput &input);
const char *DispositionName(Disposition disposition);

// TESObjectWEAP::eWeaponType values (JG GameForms.h `TESObjectWEAP::EWeaponType`; CombatHandler.cpp static_asserts that
// they match the header the plugin compiles against).
namespace WeaponType {
inline constexpr int HandToHandMelee = 0;
inline constexpr int OneHandMelee = 1;
inline constexpr int TwoHandMelee = 2;
inline constexpr int OneHandPistol = 3;
inline constexpr int OneHandPistolEnergy = 4;
inline constexpr int TwoHandRifle = 5;
inline constexpr int TwoHandAutomatic = 6;
inline constexpr int TwoHandRifleEnergy = 7;
inline constexpr int TwoHandHandle = 8;
inline constexpr int TwoHandLauncher = 9;
inline constexpr int OneHandGrenade = 10;
inline constexpr int OneHandMine = 11;
inline constexpr int OneHandLunchboxMine = 12;
inline constexpr int OneHandThrown = 13;
} // namespace WeaponType

// TESObjectWEAP::eWeaponType grouped by how the attack is performed.
enum class WeaponKind { Unarmed, Melee, Ranged, Thrown, Placed };
WeaponKind ClassifyWeapon(int weaponType);
bool NeedsReach(WeaponKind kind);
const char *WeaponKindName(WeaponKind kind);

enum class StopReason {
  Requested,
  TargetDead,
  TargetGone,
  ObjectFinished,
  OutOfAmmo,
  NoEffect,
  LineOfSightLost,
  Stalled,
  Unreachable, // the walk toward the target (melee reach, or a clear view of it) failed
  ControlsDisabled,
  NoWeapon,
  PlayerDied,
  AreaChanged,
};
// A sentence for Neuro explaining why the engagement ended. `target` is the name of what was being attacked;
// `soughtSight` is whether the engagement walked toward a target it could not see (attack's `approach`), which changes
// what a lost line of sight means and what to suggest.
std::string DescribeStop(StopReason reason, const std::string &target, bool soughtSight = false);

struct EngagementInput {
  float dt = 0.0f;
  bool paused = false;        // a menu, VATS or similar owns the player: freeze timers, release the trigger
  bool fightDisabled = false; // the player's fight control is disabled
  bool targetPresent = true;  // the reference still exists in this area
  bool targetAlive = true;    // actors only; objects are always alive
  bool lineOfSight = false;
  float targetHealth = -1.0f; // actors only, -1 unknown; used to notice attacks that do no harm
  float distance = 0.0f;
  WeaponKind weapon = WeaponKind::Ranged;
  bool automatic = false;
  float meleeRange = 100.0f;
  bool weaponDrawn = false;
  bool usesAmmo = false;
  int clip = -1;    // rounds in the magazine, -1 unknown
  int reserve = -1; // rounds in the inventory, -1 unknown
  bool aimed = false;
  bool externalMovement = false; // Neuro's own walk command is moving the player
  float travelled = 0.0f;        // ground the player has covered since the engagement began (any direction)
  bool approachFailed = false;   // the walk `approach` asked for gave up (Walker reported a failure)
};

struct EngagementOutput {
  bool aim = false;
  bool fire = false;
  bool tapReadyWeapon = false; // draw the weapon
  bool tapReload = false;
  // Walk toward the target: a melee target out of reach, or (an engagement begun with `seekSight`) a target that is
  // not in view. Never while something else is moving the player.
  bool approach = false;
  std::optional<StopReason> stop;
};

// Decides, frame by frame, what the player should be doing to keep attacking one target. Pure state machine.
class Engagement {
public:
  // Longer than any pulse "off" time, so `m_pulseTimer >= off` holds.
  static constexpr float kPulseReady = 10.0f;

  // After the target comes into view, the walk toward it goes on this long: she ends up clear of the corner, not on
  // the edge where the view flickers between blocked and clear.
  static constexpr float kSightHold = 0.3f;

  // Free-aim targets (non-actors) finish after a bounded burst because they have no health to observe. `seekSight`
  // (attack's `approach`, todo 55): without a line of sight she walks toward the target until she has one, instead of
  // waiting out the line-of-sight limit where she stands.
  void Begin(bool targetIsActor, bool seekSight = false);
  EngagementOutput Update(const EngagementInput &input);
  bool IsActive() const { return m_active; }
  void End() { m_active = false; }
  // The last reason attacking could not proceed, for diagnostics and `Stalled` messages.
  const char *Blocker() const { return m_blocker; }

private:
  bool m_active = false;
  bool m_targetIsActor = true;
  bool m_seekSight = false;
  bool m_seekingSight = false; // walking toward the target for a view of it (kept through kSightHold)
  float m_sightTime = 0.0f;    // how long the target has been in view
  bool m_triggerDown = false;
  // Time since the trigger last went up. Starts "ready" so the first shot is not delayed by the off time, and is reset
  // to ready whenever the weapon cannot fire (the next frame it can, the trigger goes down at once).
  float m_pulseTimer = kPulseReady;
  float m_losLostTime = 0.0f;
  float m_stallTime = 0.0f;
  float m_approachTime = 0.0f;
  float m_approachMark = 0.0f; // the progress measure at the start of the current window
  bool m_approachMeasured = false;
  bool m_approachBySight = false;
  bool m_approachProgressing = true;
  float m_emptyClipTime = 0.0f;
  float m_disabledTime = 0.0f;
  float m_drawCooldown = 0.0f;
  float m_reloadCooldown = 0.0f;
  float m_freeAimTime = 0.0f;
  float m_lastTargetHealth = -1.0f;
  float m_ineffectiveTime = 0.0f;
  int m_pulses = 0;
  const char *m_blocker = "";
};

// What the engine reports about the player, reduced to the values Neuro hears about during combat.
struct PlayerSnapshot {
  float health = 0.0f;
  float maxHealth = 0.0f;
  float radiation = 0.0f;
  std::string weaponName;
  WeaponKind weapon = WeaponKind::Unarmed;
  bool usesAmmo = false;
  int clip = -1;
  int reserve = -1;
};

// Compares successive snapshots and returns short sentences for meaningful changes only: health crossing a band,
// a radiation stage, a weapon change, an empty magazine or empty inventory. Never streams small changes. Outside combat
// only health and radiation are reported (falls, radiation ticks, traps); weapon and ammo changes are tracked silently
// so entering a fight does not announce stale ones.
class CombatReporter {
public:
  void Reset();
  void Seed(const PlayerSnapshot &snapshot);
  std::vector<std::string> Update(const PlayerSnapshot &snapshot, bool inCombat = true);

private:
  bool m_seeded = false;
  int m_healthBand = 0;
  int m_radStage = 0;
  std::string m_weaponName;
  bool m_clipEmptyReported = false;
  bool m_outOfAmmoReported = false;
};

// Whether a ranged shot is lined up. Designer, 2026-11-08: the first shots of a burst went out while the view was still
// turning onto the target and missed. Lined up means the remaining view error is within the tolerance *and* the view
// has stopped closing in: the combined error (heading plus pitch) changed by less than kSettleRate per second for
// kSettleSeconds. The aim controllers approach the target exponentially, so "stopped closing in" only happens once the
// error is a few thousandths of a radian; a target the view follows at a steady lag (a walking actor) also settles.
class AimGate {
public:
  static constexpr float kSettleSeconds = 0.15f;
  static constexpr float kSettleRate = 0.05f; // radians per second, the same settle rate the Walker interaction uses

  void Reset();
  bool Update(float dt, float headingError, float pitchError, float headingTolerance, float pitchTolerance);

private:
  float m_lastError = 0.0f;
  float m_still = 0.0f;
  bool m_hasError = false;
};

// The angle from a target's centre to its edge, seen from `distance` away, for a target of `radius` (its mesh bound),
// capped at `fallback`. `fallback` itself when either input is unusable.
float AngularRadius(float radius, float distance, float fallback);

// Health bands: the number of thresholds in the table (75/50/25/10 %) the health fraction is below, 0 to kBandCritical.
inline constexpr int kBandDropping = 1;
inline constexpr int kBandLow = 2;
inline constexpr int kBandVeryLow = 3;
inline constexpr int kBandCritical = 4;
int HealthBand(float health, float maxHealth);
int RadiationStage(float radiation);

inline constexpr char kApproachProperty[] = "approach";

struct AttackRequest {
  std::optional<int> enemyId;
  std::optional<int> objectId;
  bool approach = false; // walk toward a target that is not in view until it is
};
// Exactly one of enemy_id / object_id, both positive integers, and optionally the boolean `approach`.
bool ParseAttackRequest(std::string_view json, AttackRequest &request, std::string &error);
// An optional enemy_id, used by open_vats.
bool ParseOptionalEnemyId(std::string_view json, std::optional<int> &enemyId, std::string &error);

// "48/120 (40%)"
std::string FormatFraction(float value, float maximum);

} // namespace Combat
