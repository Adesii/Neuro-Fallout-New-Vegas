#include "StanceHandler.hpp"
#include "Actions/Gameplay/CrouchToggleAction.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "GameAPI.h"
#include "GameData.h"
#include "GameOSDepend.h"
#include "GameObjects.h"
#include "GameplayHandler.hpp"
#include "Movement/StanceLogic.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "utils/InputUtils.hpp"
#include "utils/TimeUtils.hpp"
#include <algorithm>
#include <memory>

namespace StanceHandler {
namespace {

// Long enough for the game's per-frame input polling to see the press.
constexpr float kTapSeconds = 0.12f;

Actions::PersistentActionSet g_actions;
InputUtils::PulseQueue g_pulses{InputCore::Owner::StancePulse};
Stance::CrouchVerifier g_verifier;

void BuildActions() {
  if (!g_actions.Empty())
    return;
  g_actions.Add(std::make_unique<Actions::Gameplay::CrouchToggleAction>());
}

// False when the mover is not available yet (loading, no game).
bool ReadCrouching(PlayerCharacter *player, bool &crouching) {
  if (!player || !player->actorMover)
    return false;
  crouching = Stance::IsSneakingFlags(player->GetMovementFlags());
  return true;
}

void CancelPending() {
  g_pulses.ReleaseAll();
  g_verifier.Cancel();
}

} // namespace

void Process(bool gameplayBlocked) {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player || !GameplayHandler::IsReady()) {
    CancelPending();
    g_actions.Unregister();
    return;
  }
  BuildActions();
  g_actions.Sync(!gameplayBlocked);

  const float deltaTime = TimeUtils::FrameSeconds();
  g_pulses.Advance(deltaTime);
  // The state is only meaningful with gameplay running: a menu would freeze the toggle mid-way.
  if (gameplayBlocked || !g_verifier.IsPending())
    return;
  bool crouching = false;
  if (!ReadCrouching(player, crouching))
    return;
  switch (g_verifier.Update(deltaTime, crouching)) {
  case Stance::Outcome::Changed:
    _MESSAGE("Crouch toggled: now %s", crouching ? "crouching" : "standing");
    NeuroSDK::SendContext(Stance::DescribeCrouched(crouching).c_str(), true);
    break;
  case Stance::Outcome::NoEffect:
    _WARNING("Crouch toggle had no effect (still %s)", crouching ? "crouching" : "standing");
    NeuroSDK::SendContext(Stance::DescribeNoEffect(crouching).c_str(), false);
    break;
  case Stance::Outcome::Pending:
    break;
  }
}

void Reset() {
  CancelPending();
  g_actions.Unregister();
}

bool ValidateCrouchToggle(std::string &error) {
  if (!GameplayHandler::ValidateGameplayAction(error))
    return false;
  auto *player = PlayerCharacter::GetSingleton();
  bool crouching = false;
  if (!player || player->GetDead() || !ReadCrouching(player, crouching)) {
    error = "You cannot change your stance right now.";
    return false;
  }
  if (player->pcControlFlags & PlayerCharacter::kControlFlag_Sneak) {
    error = "Crouching is disabled right now.";
    return false;
  }
  if (g_verifier.IsPending()) {
    error = "You are already changing your stance. Wait a moment.";
    return false;
  }
  if (InputUtils::ResolveControl(static_cast<int>(ControlCode::Crouch)) == InputUtils::kNoControl) {
    error = "The crouch control is not bound to any key.";
    return false;
  }
  return true;
}

void ToggleCrouch() {
  auto *player = PlayerCharacter::GetSingleton();
  bool crouching = false;
  if (!ReadCrouching(player, crouching))
    return;
  g_verifier.Begin(crouching);
  g_pulses.Pulse(InputUtils::ResolveControl(static_cast<int>(ControlCode::Crouch)), kTapSeconds);
  _MESSAGE("Crouch toggle requested (currently %s)", crouching ? "crouching" : "standing");
}

} // namespace StanceHandler
