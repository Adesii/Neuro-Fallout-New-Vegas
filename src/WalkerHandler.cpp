#include "WalkerHandler.hpp"
#include "CachedScripts.hpp"
#include "common.hpp"
#include "common/IDebugLog.h"
#include "nvse/GameForms.h"
namespace Walker {

CREATE_PLUGINSCRIPT(SetAutoMove, float, walk);

TESForm *g_currentObjectiveTarget;

TESForm *findcurrentobjectivetarget() {
  auto player = PlayerCharacter::GetSingleton();
  if (!player)
    return nullptr;
  _MESSAGE("Finding current objective target for player: %s", player->GetName());
  auto targetslist = player->GetCurrentQuestObjectiveTargets();
  if (!player->quest || !targetslist) {
    _MESSAGE("Player has no active quests.");
    return nullptr;
  }
  // Use the built-in xNVSE tList Iterator mechanics
  for (auto iter = targetslist->Begin(); !iter.End(); ++iter) {
    BGSQuestObjective::Target *objective = iter.Get(); // Note: might be iter.Info() depending on header version
    if (objective)
      _MESSAGE("Checking objective target: %s", objective && objective->target ? objective->target->GetName() : "None");

    if (objective && objective->target) {
      // Returns the TESObjectREFR* which automatically casts up to TESForm*
      return objective->target;
    }
  }
  return nullptr;
}

TESForm *GetCurrentObjectiveTarget() { return g_currentObjectiveTarget; }

void Process() {
  if (!g_currentObjectiveTarget) {
    CachedScripts::CallNoResult(kSetAutoMoveScript, CachedScripts::FloatArg(0.0f));
    g_currentObjectiveTarget = findcurrentobjectivetarget();
    return;
  }
  CachedScripts::CallNoResult(kSetAutoMoveScript, CachedScripts::FloatArg(1.0f));
}

} // namespace Walker
