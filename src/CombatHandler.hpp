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
bool IsEngaged();
// True for an actor that is explicitly friendly (ally/friend relation or teammate) and not fighting the player.
bool IsExplicitlyFriendly(TESObjectREFR *ref);

void QueryEnemies();
// Resolves an id from the last query_enemies. Fails for stale ids, dead and explicitly friendly actors.
bool ResolveEnemy(int id, TargetSelection &selection, std::string &error);

bool ValidateAttack(const Combat::AttackRequest &request, TargetSelection &selection, std::string &error);
bool RevalidateAttack(const TargetSelection &selection, std::string &error);
void StartAttack(const TargetSelection &selection);

bool ValidateStopAttack(std::string &error);
void StopAttack();

// Turns the view toward a target for the VATS opener. Returns true once the crosshair is on it.
bool AimAtTarget(const TargetSelection &selection, float deltaTime);

// The player's health, radiation, AP and weapon/ammo as Markdown bullets under a "## Your status" heading.
std::string BuildPlayerStatus();
float FrameTime();

} // namespace CombatHandler
