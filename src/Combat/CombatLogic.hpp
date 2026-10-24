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
  ControlsDisabled,
  NoWeapon,
  PlayerDied,
  AreaChanged,
};
// A sentence for Neuro explaining why the engagement ended. `target` is the name of what was being attacked.
std::string DescribeStop(StopReason reason, const std::string &target);

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
};

struct EngagementOutput {
  bool aim = false;
  bool fire = false;
  bool tapReadyWeapon = false; // draw the weapon
  bool tapReload = false;
  bool approach = false; // melee target out of reach and nothing else is moving the player
  std::optional<StopReason> stop;
};

// Decides, frame by frame, what the player should be doing to keep attacking one target. Pure state machine.
class Engagement {
public:
  // Free-aim targets (non-actors) finish after a bounded burst because they have no health to observe.
  void Begin(bool targetIsActor);
  EngagementOutput Update(const EngagementInput &input);
  bool IsActive() const { return m_active; }
  void End() { m_active = false; }
  // The last reason attacking could not proceed, for diagnostics and `Stalled` messages.
  const char *Blocker() const { return m_blocker; }

private:
  bool m_active = false;
  bool m_targetIsActor = true;
  bool m_triggerDown = false;
  float m_pulseTimer = 0.0f;
  float m_losLostTime = 0.0f;
  float m_stallTime = 0.0f;
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

int HealthBand(float health, float maxHealth);
int RadiationStage(float radiation);

struct AttackRequest {
  std::optional<int> enemyId;
  std::optional<int> objectId;
};
// Exactly one of enemy_id / object_id, both positive integers.
bool ParseAttackRequest(std::string_view json, AttackRequest &request, std::string &error);
// An optional enemy_id, used by open_vats.
bool ParseOptionalEnemyId(std::string_view json, std::optional<int> &enemyId, std::string &error);

// "48/120 (40%)"
std::string FormatFraction(float value, float maximum);

} // namespace Combat
