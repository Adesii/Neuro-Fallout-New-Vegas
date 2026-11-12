#pragma once

#include "Combat/CombatLogic.hpp"
#include <cstdint>
#include <string>

class PlayerCharacter;
class TESObjectREFR;

// World combat: enemy queries, the attack engagement, and the context Neuro hears while fighting. VATS is a
// separate menu owned by Menus::VatsHandler, which builds on the enemy lookup and aiming exposed here.
namespace CombatHandler {

struct TargetSelection {
  uint32_t referenceFormId = 0;
  uint32_t cellFormId = 0;
  std::string name;
  bool isActor = true;
};

void Process(bool gameplayBlocked);
void Reset();
// Registers the OnHit listener that attributes damage to the player. Once xNVSE's events exist (DeferredInit).
void InstallHitEvent();
bool IsEngaged();
// True from the combat-start context message to the combat-end one (the game's "in combat" state, debounced).
bool IsInCombat();
// True for an actor that is explicitly friendly (ally/friend relation or teammate) and not fighting the player.
bool IsExplicitlyFriendly(TESObjectREFR *ref);

void QueryEnemies();
// Resolves an id from the last query_enemies. Fails for stale ids, dead and explicitly friendly actors.
bool ResolveEnemy(int id, TargetSelection &selection, std::string &error);

// Without a line of sight the attack is refused, unless the request asks to approach (todo 55).
bool ValidateAttack(const Combat::AttackRequest &request, TargetSelection &selection, std::string &error);
bool RevalidateAttack(const TargetSelection &selection, std::string &error);
// Ends the quest route and whatever walk was running (attack is the newest command, todo 50). `approach`: walk toward
// the target whenever it is not in view, until it is.
void StartAttack(const TargetSelection &selection, bool approach);
// The walk combat started toward its target failed; `failure` is Walker's sentence about it. True when an engagement
// took it (it then ends with that sentence in its stop message), false when there is none and the caller reports it.
bool ReportApproachFailure(const std::string &failure);

bool ValidateStopAttack(std::string &error);
void StopAttack();

// Turns the view toward a target for the VATS opener. Returns true once the crosshair is on it.
bool AimAtTarget(const TargetSelection &selection, float deltaTime);

// The player's health, radiation, AP and weapon/ammo as Markdown bullets under a "## Your status" heading.
std::string BuildPlayerStatus();

} // namespace CombatHandler
