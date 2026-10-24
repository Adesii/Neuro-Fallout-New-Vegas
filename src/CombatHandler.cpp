#include "CombatHandler.hpp"
#include "Actions/Combat/AttackAction.hpp"
#include "Actions/Combat/OpenVatsAction.hpp"
#include "Actions/Combat/QueryEnemiesAction.hpp"
#include "Actions/Combat/StopAttackAction.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "Combat/DamageLogic.hpp"
#include "Context/Distance.hpp"
#include "GameAPI.h"
#include "GameData.h"
#include "GameForms.h"
#include "GameOSDepend.h"
#include "GameObjects.h"
#include "GameProcess.h"
#include "GameUI.h"
#include "Gamebryo/NiPoint3.hpp"
#include "GameplayHandler.hpp"
#include "Menus/PipBoyHandler.hpp"
#include "Menus/VatsHandler.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "WalkerHandler.hpp"
#include "utils/AimUtils.hpp"
#include "utils/InputUtils.hpp"
#include "utils/UIUtils.hpp"
#include "utils/WorldUtils.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace CombatHandler {
namespace {

using Combat::Disposition;
using WorldUtils::Distance3D;
using WorldUtils::LookupReference;

// Combat::WeaponType mirrors TESObjectWEAP::EWeaponType so the engine-free logic needs no game header.
static_assert(Combat::WeaponType::HandToHandMelee == TESObjectWEAP::kWeapType_HandToHandMelee &&
                  Combat::WeaponType::OneHandMelee == TESObjectWEAP::kWeapType_OneHandMelee &&
                  Combat::WeaponType::TwoHandMelee == TESObjectWEAP::kWeapType_TwoHandMelee &&
                  Combat::WeaponType::OneHandPistol == TESObjectWEAP::kWeapType_OneHandPistol &&
                  Combat::WeaponType::OneHandPistolEnergy == TESObjectWEAP::kWeapType_OneHandPistolEnergy &&
                  Combat::WeaponType::TwoHandRifle == TESObjectWEAP::kWeapType_TwoHandRifle &&
                  Combat::WeaponType::TwoHandAutomatic == TESObjectWEAP::kWeapType_TwoHandAutomatic &&
                  Combat::WeaponType::TwoHandRifleEnergy == TESObjectWEAP::kWeapType_TwoHandRifleEnergy &&
                  Combat::WeaponType::TwoHandHandle == TESObjectWEAP::kWeapType_TwoHandHandle &&
                  Combat::WeaponType::TwoHandLauncher == TESObjectWEAP::kWeapType_TwoHandLauncher &&
                  Combat::WeaponType::OneHandGrenade == TESObjectWEAP::kWeapType_OneHandGrenade &&
                  Combat::WeaponType::OneHandMine == TESObjectWEAP::kWeapType_OneHandMine &&
                  Combat::WeaponType::OneHandLunchboxMine == TESObjectWEAP::kWeapType_OneHandLunchboxMine &&
                  Combat::WeaponType::OneHandThrown == TESObjectWEAP::kWeapType_OneHandThrown,
              "Combat::WeaponType drifted from TESObjectWEAP::EWeaponType");

constexpr float kEnemyScanRadius = 4000.0f;
constexpr size_t kEnemyLimit = 20;
constexpr size_t kCombatStartListLimit = 5;

constexpr float kLineOfSightInterval = 0.1f;
constexpr float kAimLogInterval = 0.5f; // measurement log for todos 03 / 27 while a free-aim target is engaged

constexpr float kMeleeBaseRange = 100.0f; // matches Walker's interaction distance; scaled by weapon reach
constexpr float kRangedHeadingTolerance = 0.05f;
constexpr float kRangedPitchTolerance = 0.07f;
constexpr float kMeleeHeadingTolerance = 0.30f;
constexpr float kControlPulseSeconds = 0.12f;

constexpr float kCombatEnterDelay = 0.75f;
constexpr float kCombatExitDelay = 4.0f;
constexpr float kCombatSampleInterval = 0.25f;
constexpr float kCombatantScanInterval = 1.0f;
constexpr float kNoteInterval = 3.0f;

struct EnemyEntry {
  int id = 0;
  TargetSelection target;
  Disposition disposition = Disposition::Neutral;
  float distance = 0.0f;
  float health = 0.0f;
  float maxHealth = 0.0f;
  bool lineOfSight = false;
  bool targetingPlayer = false;
  std::string weapon;
};

struct WeaponState {
  Combat::WeaponKind kind = Combat::WeaponKind::Unarmed;
  std::string name;
  bool automatic = false;
  bool usesAmmo = false;
  bool drawn = false;
  int clip = -1;
  int reserve = -1;
  float meleeRange = kMeleeBaseRange;
};

Actions::PersistentActionSet g_worldActions;
Actions::PersistentActionSet g_stopActions;
bool g_actionsBuilt = false;
bool g_gameplayBlocked = true;

std::vector<EnemyEntry> g_enemies;

Combat::Engagement g_engagement;
bool g_wasEngaged = false;
TargetSelection g_target;
uint32_t g_startSpace = 0;
float g_lineOfSightTimer = 0.0f;
float g_aimLogTimer = 0.0f;
bool g_lineOfSight = false;
Combat::WeaponKind g_engagedWeaponKind = Combat::WeaponKind::Unarmed;

uint32_t g_attackMacro = InputUtils::kNoControl;
InputUtils::PulseQueue g_pulses{InputCore::Owner::CombatPulse};

bool g_inCombat = false;
float g_combatTimer = 0.0f;
float g_sampleTimer = 0.0f;
float g_combatantTimer = 0.0f;
float g_noteCooldown = 0.0f;
int g_kills = 0;
Combat::CombatReporter g_reporter;
std::vector<std::string> g_pendingNotes;
std::unordered_map<uint32_t, std::string> g_combatants;

// Damage and death reporting (todo 29). The OnHit handler runs inside the engine and only queues attacker ids; the
// frame loop turns them into notes.
Combat::HitNotes g_hitNotes;
Combat::DeathReporter g_deathReporter;
std::mutex g_hitQueueMutex;
std::vector<uint32_t> g_pendingHits;
bool g_hitEventInstalled = false;
float g_clock = 0.0f; // seconds of game time since the plugin started, for the per-attacker rate limit
float g_fightStartHealth = -1.0f;
float g_healthTrailTimer = 0.0f;
float g_healthPrevious = 1.0f;
float g_healthCurrent = 1.0f;
bool g_deathReported = false;

void BuildActions() {
  if (g_actionsBuilt)
    return;
  g_worldActions.Add(std::make_unique<Actions::Combat::QueryEnemiesAction>())
      .Add(std::make_unique<Actions::Combat::AttackAction>())
      .Add(std::make_unique<Actions::Combat::OpenVatsAction>());
  g_stopActions.Add(std::make_unique<Actions::Combat::StopAttackAction>());
  g_actionsBuilt = true;
}

// --- Input ----------------------------------------------------------------------------------------------------

void SetAttackHeld(bool held) {
  const uint32_t macro = held ? InputUtils::ResolveControl(static_cast<int>(ControlCode::Attack)) : g_attackMacro;
  if (held && g_attackMacro != InputUtils::kNoControl && g_attackMacro != macro)
    InputUtils::SetControlHeld(InputCore::Owner::CombatAttack, g_attackMacro, false);
  InputUtils::SetControlHeld(InputCore::Owner::CombatAttack, macro, held);
  g_attackMacro = held ? macro : InputUtils::kNoControl;
}

void PulseControl(ControlCode code) {
  g_pulses.Pulse(InputUtils::ResolveControl(static_cast<int>(code)), kControlPulseSeconds);
}

// Every exit path (pause, reset, disconnect, death, load) must end up here: a stuck mouse button is the worst failure.
void ReleaseInputs() {
  SetAttackHeld(false);
  g_pulses.ReleaseAll();
}

// --- Engine reads ---------------------------------------------------------------------------------------------

std::string NameOf(TESObjectREFR *ref) {
  const char *name = ref ? ref->GetFullName() : nullptr;
  return name && name[0] ? name : "Unnamed";
}

NiPoint3 AimPoint(TESObjectREFR *ref) { return AimUtils::TargetPoint(ref, AimUtils::Purpose::Shoot); }

uint32_t SpaceOf(TESObjectREFR *ref) {
  auto *cell = ref ? ref->GetParentCell() : nullptr;
  if (!cell)
    return 0;
  return cell->worldSpace ? cell->worldSpace->GetFormID() : cell->GetFormID();
}

bool FightingPlayer(Actor *actor, PlayerCharacter *player) {
  if (!actor->isInCombat)
    return false;
  auto *playerActor = static_cast<Actor *>(player);
  if (actor->combatTarget == playerActor)
    return true;
  auto *targets = actor->combatTargets;
  if (targets && targets->pBuffer) {
    for (uint32_t index = 0; index < targets->uiSize; ++index) {
      if (targets->pBuffer[index] == playerActor)
        return true;
    }
  }
  return false;
}

// Actor::GetFactionRelation(target, bool *isEnemy), FalloutNV.exe 1.4.0.525 0x8B87A0: __thiscall, ret 8. The same
// call is made from Actor::AttackedBy at 0x898A54 (also used by itr-nvse's AggroThreshold).
Disposition ClassifyActor(Actor *actor, PlayerCharacter *player) {
  bool isEnemy = false;
  const uint32_t relation = ThisCall<uint32_t>(0x8B87A0, actor, player, &isEnemy);
  return Combat::Classify({.relation = static_cast<int>(relation),
                           .teammate = actor->isTeammate,
                           .fightingPlayer = FightingPlayer(actor, player),
                           .aggression = actor->avOwner.GetActorValueI(eActorVal_Aggression)});
}

void ReadHealth(Actor *actor, float &current, float &maximum) {
  current = actor->avOwner.GetActorValueF(eActorVal_Health);
  maximum = actor->avOwner.GetPermanentActorValueF(eActorVal_Health);
  if (!(maximum > 0.0f))
    maximum = std::max(current, 1.0f);
}

// The HUD's ammo text is "loaded/reserve" (FalloutNVAccess/src/menus/HUDMainMenuHandler.cpp reads the same tile).
int ReadReserveFromHud() {
  auto *hud = HUDMainMenu::GetSingleton();
  if (!hud || !hud->tile058)
    return -1;
  const std::string text = UIUtils::GetTileString(hud->tile058);
  const size_t slash = text.find('/');
  if (slash == std::string::npos)
    return -1;
  size_t index = slash + 1;
  while (index < text.size() && text[index] == ' ')
    ++index;
  int value = 0;
  bool any = false;
  for (; index < text.size() && text[index] >= '0' && text[index] <= '9'; ++index) {
    value = value * 10 + (text[index] - '0');
    any = true;
  }
  return any ? value : -1;
}

MiddleHighProcess *HighProcessOf(Actor *actor) {
  auto *process = actor ? actor->baseProcess : nullptr;
  // processLevel 0 is a high process and 1 a middle-high one; both carry the weapon and ammo state.
  return process && process->processLevel <= 1 ? static_cast<MiddleHighProcess *>(process) : nullptr;
}

WeaponState ReadWeapon(PlayerCharacter *player) {
  WeaponState state;
  TESObjectWEAP *weapon = player->GetEquippedWeapon();
  auto *process = HighProcessOf(player);
  state.drawn = process && process->isWeaponOut;
  if (!weapon) {
    state.name = "Fists";
    return state;
  }
  const char *name = weapon->fullName.GetFullName();
  state.name = name && name[0] ? name : "Unnamed weapon";
  state.kind = Combat::ClassifyWeapon(weapon->eWeaponType);
  state.automatic = weapon->IsAutomatic();
  if (Combat::NeedsReach(state.kind))
    state.meleeRange = kMeleeBaseRange * std::max(1.0f, weapon->reach);
  state.usesAmmo = state.kind == Combat::WeaponKind::Ranged && weapon->ammo.ammo != nullptr;
  if (state.usesAmmo) {
    // MiddleHighProcess::ammoInfo (0x118) holds the loaded rounds; JIP-LN-NVSE's GetCurrentAmmoRounds reads it too.
    state.clip = process && process->ammoInfo ? process->ammoInfo->iNumber : -1;
    state.reserve = ReadReserveFromHud();
  }
  return state;
}

Combat::PlayerSnapshot SamplePlayer(PlayerCharacter *player) {
  Combat::PlayerSnapshot snapshot;
  float current = 0.0f;
  float maximum = 0.0f;
  ReadHealth(player, current, maximum);
  snapshot.health = current;
  snapshot.maxHealth = maximum;
  snapshot.radiation = player->avOwner.GetActorValueF(eActorVal_RadLevel);
  const WeaponState weapon = ReadWeapon(player);
  snapshot.weaponName = weapon.name;
  snapshot.weapon = weapon.kind;
  snapshot.usesAmmo = weapon.usesAmmo;
  snapshot.clip = weapon.clip;
  snapshot.reserve = weapon.reserve;
  return snapshot;
}

// --- Enemy scan -----------------------------------------------------------------------------------------------

std::vector<EnemyEntry> ScanEnemies(PlayerCharacter *player, bool withDetails) {
  std::vector<EnemyEntry> entries;
  WorldUtils::ForEachLoadedReference([&](TESObjectREFR *ref, TESObjectCELL *cell) {
    if (ref == player || !ref->IsActor())
      return;
    auto *actor = static_cast<Actor *>(ref);
    if (actor->GetDead())
      return;
    const float distance = Distance3D(player->GetPos(), ref->GetPos());
    if (!std::isfinite(distance) || distance > kEnemyScanRadius)
      return;
    const Disposition disposition = ClassifyActor(actor, player);
    if (disposition == Disposition::Friendly)
      return;
    EnemyEntry entry;
    entry.target = {ref->GetFormID(), cell->GetFormID(), NameOf(ref), true};
    entry.disposition = disposition;
    entry.distance = distance;
    entry.targetingPlayer = FightingPlayer(actor, player);
    ReadHealth(actor, entry.health, entry.maxHealth);
    entries.push_back(std::move(entry));
  });

  std::sort(entries.begin(), entries.end(), [](const EnemyEntry &left, const EnemyEntry &right) {
    if (left.disposition != right.disposition)
      return left.disposition == Disposition::Hostile;
    if (left.distance != right.distance)
      return left.distance < right.distance;
    return left.target.referenceFormId < right.target.referenceFormId;
  });
  if (entries.size() > kEnemyLimit)
    entries.resize(kEnemyLimit);

  if (withDetails) {
    for (auto &entry : entries) {
      auto *ref = LookupReference(entry.target.referenceFormId);
      if (!ref)
        continue;
      entry.lineOfSight = WorldUtils::HasLineOfSight(player, ref, AimPoint(ref));
      auto *actor = static_cast<Actor *>(ref);
      if (HighProcessOf(actor)) {
        if (TESObjectWEAP *weapon = actor->GetEquippedWeapon()) {
          const char *name = weapon->fullName.GetFullName();
          entry.weapon = name && name[0] ? name : "";
        }
      }
    }
  }
  return entries;
}

void RefreshEnemies(PlayerCharacter *player) {
  g_enemies = ScanEnemies(player, true);
  for (size_t index = 0; index < g_enemies.size(); ++index)
    g_enemies[index].id = static_cast<int>(index + 1);
}

// --- Context text ---------------------------------------------------------------------------------------------

std::string DescribeEnemyBrief(const EnemyEntry &entry) {
  std::ostringstream text;
  text << "`" << entry.id << "` " << entry.target.name << " (" << Combat::FormatFraction(entry.health, entry.maxHealth)
       << ", " << Units::FormatDistance(entry.distance) << (entry.targetingPlayer ? ", targeting you" : "") << ")";
  return text.str();
}

std::string ValidEnemyIds() {
  if (g_enemies.empty())
    return "There are no current enemy ids. Run query_enemies to refresh the list.";
  std::ostringstream stream;
  stream << "Valid enemy ids are ";
  for (size_t index = 0; index < g_enemies.size(); ++index) {
    if (index)
      stream << ", ";
    stream << g_enemies[index].id;
  }
  stream << ". Run query_enemies to refresh the list.";
  return stream.str();
}

// --- Engagement -----------------------------------------------------------------------------------------------

void FinishEngagement(Combat::StopReason reason) {
  ReleaseInputs();
  // A shot-up bottle or crate may simply be removed from the world; that is the burst working, not a failure.
  if (reason == Combat::StopReason::TargetGone && !g_target.isActor)
    reason = Combat::StopReason::ObjectFinished;
  std::string message = Combat::DescribeStop(reason, g_target.name);
  if (reason == Combat::StopReason::Stalled && g_engagement.Blocker()[0])
    message += std::string(" (") + g_engagement.Blocker() + ".)";
  g_engagement.End();
  Walker::SetLookSuppressed(false);
  if (Walker::IsActive() && Walker::GetOwner() == Walker::Owner::Combat)
    Walker::Stop();
  _MESSAGE("Combat engagement ended (%d): %s", static_cast<int>(reason), message.c_str());
  // The player's own stop_attack is already known to Neuro; everything else is news about why combat stopped.
  NeuroSDK::SendContext(message.c_str(), reason == Combat::StopReason::Requested);
}

// Measurements todo 03 / 27 ask for: where the eye, the target and the aim point are, and how far the view still is
// from the point. Only for non-actor targets (bottles, props), a couple of lines per second.
void LogFreeAim(PlayerCharacter *player, TESObjectREFR *target, const NiPoint3 &point, const AimUtils::Error &error,
                float deltaTime) {
  if (target->IsActor())
    return;
  g_aimLogTimer -= deltaTime;
  if (g_aimLogTimer > 0.0f)
    return;
  g_aimLogTimer = kAimLogInterval;
  const NiPoint3 eye = AimUtils::EyePosition(player);
  const NiPoint3 origin = target->GetPos();
  // InterfaceManager::crosshairRef (0xFC in both JG and JIP-LN-NVSE): what the crosshair rests on, for any reference.
  auto *manager = InterfaceManager::GetSingleton();
  TESObjectREFR *crosshair = manager ? manager->crosshairRef : nullptr;
  _MESSAGE("Aim %s: eye (%.1f %.1f %.1f) crouching %d scale %.2f; target origin (%.1f %.1f %.1f) scale %.2f; aim point "
           "z+%.1f; distance %.1f; yaw %.3f rad, heading error %.4f, pitch %.3f rad, pitch error %.4f; crosshair %08X "
           "(target %08X), "
           "reticle actor on target %d",
           g_target.name.c_str(), eye.x, eye.y, eye.z, AimUtils::IsCrouching(player) ? 1 : 0, player->GetScale(),
           origin.x, origin.y, origin.z, target->GetScale(), point.z - origin.z, Distance3D(eye, point),
           AimUtils::EffectiveYaw(player), error.heading, AimUtils::CameraPitch(), error.pitch,
           crosshair ? crosshair->GetFormID() : 0, target->GetFormID(),
           player->reticleActor && player->reticleActor == target ? 1 : 0);
}

bool AimIsOnTarget(PlayerCharacter *player, TESObjectREFR *target, const AimUtils::Error &error,
                   Combat::WeaponKind kind) {
  if (player->reticleActor && player->reticleActor == target)
    return true;
  const bool melee = Combat::NeedsReach(kind);
  const float heading = melee ? kMeleeHeadingTolerance : kRangedHeadingTolerance;
  if (std::abs(error.heading) > heading)
    return false;
  return melee || std::abs(error.pitch) <= kRangedPitchTolerance;
}

void ProcessEngagement(PlayerCharacter *player, float deltaTime, bool gameplayBlocked) {
  g_pulses.Advance(deltaTime);
  if (!g_engagement.IsActive()) {
    // Only the frame an engagement ends: releasing every frame wrote "no control" into the input hook constantly.
    // FinishEngagement and Reset release as well; this catches any other way the state machine went inactive.
    if (g_wasEngaged) {
      g_wasEngaged = false;
      ReleaseInputs();
      Walker::SetLookSuppressed(false);
    }
    return;
  }
  g_wasEngaged = true;
  if (player->GetDead()) {
    FinishEngagement(Combat::StopReason::PlayerDied);
    return;
  }
  if (SpaceOf(player) != g_startSpace) {
    FinishEngagement(Combat::StopReason::AreaChanged);
    return;
  }

  auto *target = LookupReference(g_target.referenceFormId);
  const bool paused = gameplayBlocked || Menus::PipBoyHandler::IsExecuting() || Menus::VatsHandler::IsActive();
  Walker::SetLookSuppressed(!paused);

  Combat::EngagementInput input;
  input.dt = deltaTime;
  input.paused = paused;
  input.targetPresent = target && SpaceOf(target) == g_startSpace;
  input.targetAlive = !(input.targetPresent && target->IsActor() && static_cast<Actor *>(target)->GetDead());
  if (!input.targetPresent || !input.targetAlive || paused) {
    const auto output = g_engagement.Update(input);
    SetAttackHeld(false);
    if (output.stop)
      FinishEngagement(*output.stop);
    return;
  }

  const WeaponState weapon = ReadWeapon(player);
  // The last grenade or thrown weapon leaves the hand empty; do not turn that into an unarmed charge.
  if (g_engagedWeaponKind == Combat::WeaponKind::Thrown && weapon.kind != Combat::WeaponKind::Thrown) {
    FinishEngagement(Combat::StopReason::OutOfAmmo);
    return;
  }
  g_engagedWeaponKind = weapon.kind;
  const NiPoint3 point = AimPoint(target);
  g_lineOfSightTimer -= deltaTime;
  if (g_lineOfSightTimer <= 0.0f) {
    const bool lineOfSight = WorldUtils::HasLineOfSight(player, target, point);
    if (lineOfSight != g_lineOfSight)
      _DMESSAGE("Combat line of sight to %s changed: %s", g_target.name.c_str(), lineOfSight ? "clear" : "blocked");
    g_lineOfSight = lineOfSight;
    g_lineOfSightTimer = kLineOfSightInterval;
  }
  const AimUtils::Error error = AimUtils::AimAt(player, point, deltaTime);
  LogFreeAim(player, target, point, error, deltaTime);

  input.fightDisabled = (player->pcControlFlags & PlayerCharacter::kControlFlag_Fight) != 0;
  input.lineOfSight = g_lineOfSight;
  if (target->IsActor()) {
    float maximum = 0.0f;
    ReadHealth(static_cast<Actor *>(target), input.targetHealth, maximum);
  }
  input.distance = Distance3D(player->GetPos(), target->GetPos());
  input.weapon = weapon.kind;
  input.automatic = weapon.automatic;
  input.meleeRange = weapon.meleeRange;
  input.weaponDrawn = weapon.drawn;
  input.usesAmmo = weapon.usesAmmo;
  input.clip = weapon.clip;
  input.reserve = weapon.reserve;
  input.aimed = AimIsOnTarget(player, target, error, weapon.kind);
  input.externalMovement = Walker::IsActive() && Walker::GetOwner() != Walker::Owner::Combat;

  const auto output = g_engagement.Update(input);
  SetAttackHeld(output.fire);
  if (output.tapReadyWeapon || output.tapReload)
    PulseControl(ControlCode::ReadyItem); // ready-weapon draws a holstered weapon and reloads a drawn one
  if (output.approach) {
    Walker::Start(g_target.referenceFormId, Walker::Intent::Move, Walker::Owner::Combat, g_target.name);
  } else if (Walker::IsActive() && Walker::GetOwner() == Walker::Owner::Combat) {
    Walker::Stop();
  }
  if (output.stop)
    FinishEngagement(*output.stop);
}

// --- Combat context -------------------------------------------------------------------------------------------

void FlushNotes(bool force) {
  if (g_pendingNotes.empty() || (!force && g_noteCooldown > 0.0f))
    return;
  std::ostringstream text;
  text << (g_inCombat ? "## Combat update" : "## Status update");
  for (const auto &note : g_pendingNotes)
    text << "\n- " << note;
  // Cleared only once the send went through: a note that could not be delivered is retried, not lost.
  if (!NeuroSDK::SendContext(text.str().c_str(), false))
    return;
  g_pendingNotes.clear();
  g_noteCooldown = kNoteInterval;
}

void StartCombatContext(PlayerCharacter *player) {
  g_inCombat = true;
  g_kills = 0;
  g_combatants.clear();
  // Notes queued before the fight (a first hit, a fall) are kept: they go out with the first combat update.
  g_noteCooldown = 0.0f;
  g_sampleTimer = 0.0f;
  g_combatantTimer = 0.0f;
  const Combat::PlayerSnapshot snapshot = SamplePlayer(player);
  g_reporter.Seed(snapshot);
  g_fightStartHealth = snapshot.maxHealth > 0.0f ? std::clamp(snapshot.health / snapshot.maxHealth, 0.0f, 1.0f) : -1.0f;

  // Ids are published with the start message so Neuro can attack straight away without a query round trip.
  RefreshEnemies(player);
  std::vector<const EnemyEntry *> hostile;
  for (const auto &entry : g_enemies) {
    if (entry.disposition == Disposition::Hostile) {
      hostile.push_back(&entry);
      if (entry.targetingPlayer)
        g_combatants[entry.target.referenceFormId] = entry.target.name;
    }
  }
  std::stable_sort(hostile.begin(), hostile.end(), [](const EnemyEntry *left, const EnemyEntry *right) {
    return left->targetingPlayer > right->targetingPlayer;
  });

  std::ostringstream text;
  text << "## Combat started\nYou are in combat.";
  if (hostile.empty()) {
    text << "\n- No hostile enemy has been identified yet.";
  } else {
    text << "\n\n## Hostile enemies";
    for (size_t index = 0; index < hostile.size() && index < kCombatStartListLimit; ++index)
      text << "\n- " << DescribeEnemyBrief(*hostile[index]);
    if (hostile.size() > kCombatStartListLimit)
      text << "\n- and " << (hostile.size() - kCombatStartListLimit) << " more";
  }
  text << "\n\n" << BuildPlayerStatus();
  _MESSAGE("Combat started");
  NeuroSDK::SendContext(text.str().c_str(), false);
}

void EndCombatContext() {
  FlushNotes(true);
  g_inCombat = false;
  g_combatTimer = 0.0f;
  g_fightStartHealth = -1.0f;
  std::ostringstream text;
  text << "## Combat ended\n- Enemies defeated during the fight: " << g_kills << "\n\n" << BuildPlayerStatus();
  g_combatants.clear();
  g_reporter.Reset();
  _MESSAGE("Combat ended (%d kill(s))", g_kills);
  NeuroSDK::SendContext(text.str().c_str(), false);
}

void ObserveCombatants(PlayerCharacter *player) {
  for (const auto &entry : ScanEnemies(player, false)) {
    if (entry.disposition == Disposition::Hostile && entry.targetingPlayer)
      g_combatants.emplace(entry.target.referenceFormId, entry.target.name);
  }
  for (auto iterator = g_combatants.begin(); iterator != g_combatants.end();) {
    auto *ref = LookupReference(iterator->first);
    if (!ref || !ref->IsActor()) {
      iterator = g_combatants.erase(iterator);
    } else if (static_cast<Actor *>(ref)->GetDead()) {
      // The stop message already told Neuro that its attack target died.
      if (iterator->first != g_target.referenceFormId || g_engagement.IsActive())
        g_pendingNotes.push_back(iterator->second + " died.");
      ++g_kills;
      iterator = g_combatants.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

// Runs inside the engine when any reference is hit: NVSE's built-in OnHit event passes the victim as its first
// argument and the attacker as its second (xNVSE EventManager.cpp HandleGameEvent/EventCallback::Invoke). Only the
// player as victim matters. The attacker is null for damage with no attacker (falls, fire, some hazards).
void OnHitEvent(TESObjectREFR *, void *parameters) {
  if (!parameters)
    return;
  auto **args = static_cast<void **>(parameters);
  auto *victim = static_cast<TESObjectREFR *>(args[0]);
  auto *attacker = static_cast<TESForm *>(args[1]);
  auto *player = PlayerCharacter::GetSingleton();
  if (!victim || !player || victim != static_cast<TESObjectREFR *>(player))
    return;
  std::lock_guard lock(g_hitQueueMutex);
  g_pendingHits.push_back(attacker ? attacker->GetFormID() : 0);
}

std::string OptionalName(TESObjectREFR *ref) {
  const char *name = ref ? ref->GetFullName() : nullptr;
  return name && name[0] ? name : "";
}

Combat::HitInfo DescribeHit(uint32_t attackerId) {
  Combat::HitInfo hit;
  hit.attackerId = attackerId;
  auto *ref = attackerId ? LookupReference(attackerId) : nullptr;
  hit.attackerIsActor = ref && ref->IsActor();
  hit.attackerName = OptionalName(ref);
  if (hit.attackerIsActor && HighProcessOf(static_cast<Actor *>(ref))) {
    if (TESObjectWEAP *weapon = static_cast<Actor *>(ref)->GetEquippedWeapon()) {
      const char *name = weapon->fullName.GetFullName();
      hit.weapon = name && name[0] ? name : "";
    }
  }
  return hit;
}

void DrainHits() {
  std::vector<uint32_t> hits;
  {
    std::lock_guard lock(g_hitQueueMutex);
    hits.swap(g_pendingHits);
  }
  for (uint32_t attackerId : hits) {
    const Combat::HitInfo hit = DescribeHit(attackerId);
    if (auto note = g_hitNotes.Record(hit, g_clock))
      g_pendingNotes.push_back(std::move(*note));
  }
}

// Only a killer the engine's own actor list still holds is read: Actor::killer (JG GameObjects.h, offset 0xC0) is a raw
// pointer and is never dereferenced unless it is a live loaded reference. Whether the engine sets it for the player is
// unverified (docs/combat.md).
std::string LiveActorName(Actor *actor, PlayerCharacter *player) {
  if (!actor || actor == static_cast<Actor *>(player))
    return {};
  std::string name;
  WorldUtils::ForEachLoadedReference([&](TESObjectREFR *ref, TESObjectCELL *) {
    if (ref == static_cast<TESObjectREFR *>(actor))
      name = OptionalName(ref);
  });
  return name;
}

void TrackHealth(PlayerCharacter *player, float deltaTime) {
  g_healthTrailTimer += deltaTime;
  if (g_healthTrailTimer < kCombatSampleInterval)
    return;
  g_healthTrailTimer = 0.0f;
  float current = 0.0f;
  float maximum = 0.0f;
  ReadHealth(player, current, maximum);
  g_healthPrevious = g_healthCurrent;
  g_healthCurrent = std::clamp(current / maximum, 0.0f, 1.0f);
}

// One non-silent message the first frame the player is dead: the fact, what killed her and a short lesson. The game
// reloads a save afterwards, which resets this state; it is only marked reported once the message went out.
void ReportDeath(PlayerCharacter *player) {
  if (!player->GetDead()) {
    g_deathReported = false;
    return;
  }
  if (g_deathReported)
    return;
  Combat::DeathInput input;
  input.killerName = LiveActorName(static_cast<Actor *>(player)->killer, player);
  input.lastHit = g_hitNotes.Last();
  input.now = g_clock;
  input.radiation = player->avOwner.GetActorValueF(eActorVal_RadLevel);
  input.healthFractionBefore = std::max(g_healthPrevious, g_healthCurrent);
  input.healthFractionAtFightStart = g_fightStartHealth;
  input.inCombat = g_inCombat;
  if (g_engagement.IsActive())
    input.activity = "attacking " + g_target.name;
  else if (Walker::IsActive())
    input.activity =
        "walking to " + (Walker::GetDescription().empty() ? std::string("a destination") : Walker::GetDescription());
  const std::string text = g_deathReporter.Describe(input);
  _MESSAGE("Player died: %s", text.c_str());
  if (NeuroSDK::SendContext(text.c_str(), false)) {
    g_deathReported = true;
    g_pendingNotes.clear(); // anything still queued is overtaken by the death report
  }
}

void ProcessCombatContext(PlayerCharacter *player, float deltaTime) {
  const bool combat = player->pcInCombat;
  if (!g_inCombat) {
    g_combatTimer = combat ? g_combatTimer + deltaTime : 0.0f;
    if (g_combatTimer >= kCombatEnterDelay) {
      StartCombatContext(player);
      return;
    }
  } else {
    g_combatTimer = combat ? 0.0f : g_combatTimer + deltaTime;
    if (g_combatTimer >= kCombatExitDelay) {
      EndCombatContext();
      return;
    }
  }

  // Hits and banded health/radiation changes are reported inside and outside combat; a fall or a trap needs no fight.
  g_noteCooldown = std::max(0.0f, g_noteCooldown - deltaTime);
  DrainHits();
  g_sampleTimer += deltaTime;
  if (g_sampleTimer >= kCombatSampleInterval) {
    g_sampleTimer = 0.0f;
    for (auto &note : g_reporter.Update(SamplePlayer(player), g_inCombat))
      g_pendingNotes.push_back(std::move(note));
  }
  if (g_inCombat) {
    g_combatantTimer += deltaTime;
    if (g_combatantTimer >= kCombatantScanInterval) {
      g_combatantTimer = 0.0f;
      ObserveCombatants(player);
    }
  }
  FlushNotes(false);
}

bool CheckAttackTarget(PlayerCharacter *player, TESObjectREFR *ref, std::string &error) {
  if (!ref || !ref->GetParentCell() || SpaceOf(ref) != SpaceOf(player)) {
    error = "That target is no longer available in the current area.";
    return false;
  }
  if (ref->IsActor()) {
    auto *actor = static_cast<Actor *>(ref);
    if (actor->GetDead()) {
      error = NameOf(ref) + " is already dead.";
      return false;
    }
    if (ClassifyActor(actor, player) == Disposition::Friendly) {
      error = NameOf(ref) + " is friendly and cannot be attacked.";
      return false;
    }
  }
  return true;
}

} // namespace

// --- Public interface -----------------------------------------------------------------------------------------

float FrameTime() {
  auto *time = TimeGlobal::Get();
  return time ? std::clamp(time->secondsPassed, 0.0f, 0.1f) : 0.0f;
}

void Process(bool gameplayBlocked) {
  g_gameplayBlocked = gameplayBlocked;
  auto *player = PlayerCharacter::GetSingleton();
  if (!player || !GameplayHandler::IsReady()) {
    if (g_engagement.IsActive() || g_attackMacro != InputUtils::kNoControl)
      Reset();
    g_worldActions.Unregister();
    g_stopActions.Unregister();
    return;
  }
  BuildActions();
  // stop_attack must stay available while a menu is open: the engagement survives menus.
  g_stopActions.Register();
  if (gameplayBlocked)
    g_worldActions.Unregister();
  else
    g_worldActions.Register();

  const float deltaTime = FrameTime();
  g_clock += deltaTime;
  TrackHealth(player, deltaTime);
  ProcessEngagement(player, deltaTime, gameplayBlocked);
  // Death is reported whatever menu is up: the game shows its own screens before it reloads a save.
  ReportDeath(player);
  if (!gameplayBlocked && !player->GetDead())
    ProcessCombatContext(player, deltaTime);
}

void Reset() {
  ReleaseInputs();
  g_engagement.End();
  g_wasEngaged = false;
  Walker::SetLookSuppressed(false);
  g_worldActions.Unregister();
  g_stopActions.Unregister();
  g_gameplayBlocked = true;
  g_enemies.clear();
  g_target = {};
  g_lineOfSightTimer = 0.0f;
  g_lineOfSight = false;
  g_inCombat = false;
  g_combatTimer = 0.0f;
  g_noteCooldown = 0.0f;
  g_kills = 0;
  g_reporter.Reset();
  g_pendingNotes.clear();
  g_combatants.clear();
  g_hitNotes.Reset();
  {
    std::lock_guard lock(g_hitQueueMutex);
    g_pendingHits.clear();
  }
  g_fightStartHealth = -1.0f;
  g_healthTrailTimer = 0.0f;
  g_healthPrevious = 1.0f;
  g_healthCurrent = 1.0f;
  g_deathReported = false;
}

void InstallHitEvent() {
  if (g_hitEventInstalled)
    return;
  if (!g_eventInterface || !g_eventInterface->SetNativeEventHandler) {
    _WARNING("The xNVSE event manager is unavailable; hits on the player cannot be attributed.");
    return;
  }
  g_hitEventInstalled = g_eventInterface->SetNativeEventHandler("OnHit", OnHitEvent);
  if (g_hitEventInstalled)
    _MESSAGE("Listening for OnHit events to attribute damage to the player");
  else
    _WARNING("Could not register the OnHit event handler; hits on the player cannot be attributed.");
}

bool IsEngaged() { return g_engagement.IsActive(); }

bool IsInCombat() { return g_inCombat; }

bool IsExplicitlyFriendly(TESObjectREFR *ref) {
  auto *player = PlayerCharacter::GetSingleton();
  return player && ref && ref->IsActor() && ClassifyActor(static_cast<Actor *>(ref), player) == Disposition::Friendly;
}

std::string BuildPlayerStatus() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return "## Your status\n- Unavailable.";
  float health = 0.0f;
  float maxHealth = 0.0f;
  ReadHealth(player, health, maxHealth);
  const float radiation = player->avOwner.GetActorValueF(eActorVal_RadLevel);
  const float actionPoints = player->avOwner.GetActorValueF(eActorVal_ActionPoints);
  const float maxActionPoints = player->avOwner.GetPermanentActorValueF(eActorVal_ActionPoints);
  const WeaponState weapon = ReadWeapon(player);

  std::ostringstream text;
  text << "## Your status\n- Health: " << Combat::FormatFraction(health, maxHealth);
  text << "\n- Radiation: " << static_cast<int>(radiation) << " rads";
  text << "\n- Action points: " << static_cast<int>(actionPoints) << "/" << static_cast<int>(maxActionPoints);
  text << "\n- Weapon: " << weapon.name << " (" << Combat::WeaponKindName(weapon.kind)
       << (weapon.kind != Combat::WeaponKind::Unarmed && !weapon.drawn ? ", holstered" : "") << ")";
  if (weapon.usesAmmo) {
    if (weapon.clip >= 0)
      text << ", " << weapon.clip << " loaded";
    if (weapon.reserve >= 0)
      text << ", " << weapon.reserve << " in reserve";
  }
  if (g_engagement.IsActive())
    text << "\n- Currently attacking: " << g_target.name;
  return text.str();
}

void QueryEnemies() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return;
  RefreshEnemies(player);

  std::ostringstream context;
  context << "## Nearby enemies";
  if (g_enemies.empty()) {
    context << "\n- None found nearby.";
  } else {
    context << "\nHostile actors are listed first. Neutral actors are not fighting you, but you may attack them if "
               "you choose. Explicitly friendly actors are never listed.";
    for (const auto &entry : g_enemies) {
      context << "\n- `" << entry.id << "` - **" << Combat::DispositionName(entry.disposition) << "** - "
              << entry.target.name << " - health " << Combat::FormatFraction(entry.health, entry.maxHealth) << " - "
              << Units::FormatDistance(entry.distance) << " away - "
              << (entry.lineOfSight ? "clear line of sight" : "no line of sight (behind cover or a wall)");
      if (!entry.weapon.empty())
        context << " - wielding " << entry.weapon;
      if (entry.targetingPlayer)
        context << " - targeting you";
    }
  }
  context << "\n\n" << BuildPlayerStatus();
  NeuroSDK::SendContext(context.str().c_str(), true);
}

bool ResolveEnemy(int id, TargetSelection &selection, std::string &error) {
  auto entry =
      std::find_if(g_enemies.begin(), g_enemies.end(), [id](const EnemyEntry &enemy) { return enemy.id == id; });
  if (entry == g_enemies.end()) {
    error = ValidEnemyIds();
    return false;
  }
  auto *player = PlayerCharacter::GetSingleton();
  auto *ref = LookupReference(entry->target.referenceFormId);
  if (!player || !CheckAttackTarget(player, ref, error))
    return false;
  selection = entry->target;
  selection.isActor = true;
  return true;
}

bool ValidateAttack(const Combat::AttackRequest &request, TargetSelection &selection, std::string &error) {
  if (!GameplayHandler::ValidateGameplayAction(error))
    return false;
  auto *player = PlayerCharacter::GetSingleton();
  if (player->GetDead()) {
    error = "You cannot attack while dead.";
    return false;
  }
  if (player->pcControlFlags & PlayerCharacter::kControlFlag_Fight) {
    error = "Your fight controls are currently disabled, so you cannot attack right now.";
    return false;
  }
  if (Combat::ClassifyWeapon(player->GetEquippedWeapon() ? player->GetEquippedWeapon()->eWeaponType : 0) ==
      Combat::WeaponKind::Placed) {
    error = "Your equipped weapon is a placed explosive and cannot be used to attack. Equip another weapon first.";
    return false;
  }

  if (request.enemyId) {
    if (!ResolveEnemy(*request.enemyId, selection, error))
      return false;
  } else {
    GameplayHandler::ObjectSelection object;
    if (!GameplayHandler::ValidateObjectSelection(*request.objectId, object, error))
      return false;
    auto *ref = LookupReference(object.referenceFormId);
    if (!CheckAttackTarget(player, ref, error))
      return false;
    selection = {object.referenceFormId, object.cellFormId, object.name, ref->IsActor()};
  }

  auto *ref = LookupReference(selection.referenceFormId);
  if (!ref || !WorldUtils::HasLineOfSight(player, ref, AimPoint(ref))) {
    error = selection.name +
            " is not in your line of sight (behind a wall or cover). Move to get a clear view, then try again.";
    return false;
  }
  return true;
}

bool RevalidateAttack(const TargetSelection &selection, std::string &error) {
  if (!GameplayHandler::ValidateGameplayAction(error))
    return false;
  auto *player = PlayerCharacter::GetSingleton();
  return player && CheckAttackTarget(player, LookupReference(selection.referenceFormId), error);
}

void StartAttack(const TargetSelection &selection) {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return;
  const bool retarget = g_engagement.IsActive();
  if (Walker::IsActive() && Walker::GetOwner() == Walker::Owner::Combat)
    Walker::Stop();
  g_target = selection;
  g_startSpace = SpaceOf(player);
  g_lineOfSightTimer = 0.0f;
  g_lineOfSight = true;
  g_engagedWeaponKind = ReadWeapon(player).kind;
  g_engagement.Begin(selection.isActor);
  _MESSAGE("Combat engagement started against %s (%08X)%s", selection.name.c_str(), selection.referenceFormId,
           retarget ? " [retarget]" : "");

  std::ostringstream text;
  text << "## Attacking\nYou are " << (retarget ? "now " : "") << "attacking " << selection.name << ".";
  if (selection.isActor)
    text << " You will keep attacking until it dies, you run out of ammo, or you lose sight of it for too long. ";
  else
    text << " You will fire a short burst at it. ";
  text << "You can still move, switch weapons, call attack again to change target, or stop_attack to stop.";
  NeuroSDK::SendContext(text.str().c_str(), true);
}

bool ValidateStopAttack(std::string &error) {
  if (!PlayerCharacter::GetSingleton() || !GameplayHandler::IsReady()) {
    error = "Combat actions are unavailable until a game is loaded.";
    return false;
  }
  if (!g_engagement.IsActive()) {
    error = "You are not attacking anything right now.";
    return false;
  }
  return true;
}

void StopAttack() {
  if (g_engagement.IsActive())
    FinishEngagement(Combat::StopReason::Requested);
}

bool AimAtTarget(const TargetSelection &selection, float deltaTime) {
  auto *player = PlayerCharacter::GetSingleton();
  auto *ref = LookupReference(selection.referenceFormId);
  if (!player || !ref)
    return false;
  const AimUtils::Error error = AimUtils::AimAt(player, AimPoint(ref), deltaTime);
  return AimIsOnTarget(player, ref, error, Combat::WeaponKind::Ranged);
}

} // namespace CombatHandler
