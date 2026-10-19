#include "VatsHandler.hpp"
#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "Actions/Combat/VatsCancelAction.hpp"
#include "Actions/Combat/VatsExecuteAction.hpp"
#include "Actions/Combat/VatsQueueShotAction.hpp"
#include "GameAPI.h"
#include "GameOSDepend.h"
#include "GameObjects.h"
#include "GameTiles.h"
#include "GameUI.h"
#include "GameplayHandler.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "defs/VatsMenu.hpp"
#include "utils/InputUtils.hpp"
#include "utils/ScopedState.hpp"
#include "utils/UIUtils.hpp"
#include "utils/WorldUtils.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <vector>

namespace Menus::VatsHandler {
namespace {

using Clock = std::chrono::steady_clock;
using WorldUtils::LookupReference;

constexpr auto kStepDelay = std::chrono::milliseconds(350);
constexpr auto kSwitchDelay = std::chrono::milliseconds(650);
constexpr auto kAimTimeout = std::chrono::seconds(3);
constexpr auto kOpenTimeout = std::chrono::seconds(3);
constexpr auto kVerifyTimeout = std::chrono::milliseconds(1800);
constexpr auto kPlaybackStartTimeout = std::chrono::seconds(4);
constexpr auto kCancelTimeout = std::chrono::seconds(4);
// Playback and the kill camera are brief; a mode that never returns to none must not lock the game out for good.
constexpr auto kPlaybackWatchdog = std::chrono::seconds(90);
constexpr auto kRegisterRetryDelay = std::chrono::seconds(2);
constexpr float kPulseSeconds = 0.12f;
constexpr uint32_t kProjectileTargetType = 2; // itr-nvse VATSTargetType_Projectile: not an actor to shoot

// Control paths are children of the menu's GlowBranch rect (vats_menu.xml; FalloutNVAccess reads them the same way).
constexpr const char *kAcceptButton = "GlowBranch/accept_button";
constexpr const char *kReturnButton = "GlowBranch/return_button";
constexpr const char *kRightArrow = "GlowBranch/right_arrow";

struct ListedPart {
  uint32_t id = 0;
  std::string name;
  std::string chance;
};

struct ListedTarget {
  uint32_t formId = 0;
  std::string name;
  float health = 0.0f;
  float maxHealth = 0.0f;
  std::vector<ListedPart> parts;
};

enum class ExecKind { Shot, Execute, Cancel };
enum class ShotPhase { SelectTarget, SelectLimb, Fire, Verify };

struct Execution {
  ExecKind kind = ExecKind::Shot;
  Menu *owner = nullptr;
  ShotRequest shot;
  int partCount = 1;
  int targetCount = 1;
  ShotPhase phase = ShotPhase::SelectTarget;
  Clock::time_point nextStepAt;
  Clock::time_point deadline;
  int attempts = 0;
  int queuedBefore = 0;
  float apBefore = 0.0f;
  bool clicked = false;
};

struct OpenState {
  std::optional<CombatHandler::TargetSelection> target;
  Clock::time_point startedAt;
  Clock::time_point pressedAt;
  bool pressed = false;
};

std::unique_ptr<Actions::ActionWindow> g_window;
ScopedState::Backoff g_registerRetry{kRegisterRetryDelay};
std::vector<ListedTarget> g_listing;
std::unique_ptr<Execution> g_execution;
std::unique_ptr<OpenState> g_open;
bool g_playback = false;
bool g_active = false;
Clock::time_point g_playbackSince{};
bool g_playbackWatchdogLogged = false;
InputUtils::PulseQueue g_pulses;

bool IsSelecting(Menu *menu) {
  const uint32_t mode = Vats::GetMode();
  return menu && UIUtils::IsTopMenu(menu, Interface::VATS) &&
         (mode == Vats::kModeTargetSelection || mode == Vats::kModeLimbSelection);
}

// True while the engine reports attack playback. The mode global alone is not trusted to return to none.
bool PlaybackBlocks(uint32_t mode) {
  if (mode != Vats::kModePlayback) {
    g_playbackSince = {};
    g_playbackWatchdogLogged = false;
    return false;
  }
  const auto now = Clock::now();
  if (g_playbackSince == Clock::time_point{})
    g_playbackSince = now;
  if (now - g_playbackSince > kPlaybackWatchdog) {
    if (!g_playbackWatchdogLogged)
      _WARNING("VATS playback mode did not end after %lld s; no longer blocking gameplay",
               static_cast<long long>(kPlaybackWatchdog.count()));
    g_playbackWatchdogLogged = true;
    return false;
  }
  return true;
}

void PulseControl(ControlCode code) {
  g_pulses.Pulse(InputUtils::ResolveControl(static_cast<int>(code)), kPulseSeconds);
}

std::string LimbNameOf(const Vats::BodyPart *part) {
  static const uint32_t trait = Tile::TraitNameToID("_LimbName");
  if (part && part->percentTile) {
    auto *value = part->percentTile->GetValue(trait);
    if (value && value->str && value->str[0])
      return value->str;
  }
  return "Part " + std::to_string(part ? part->bodyPartId : 0);
}

std::string ChanceOf(const Vats::BodyPart *part) {
  if (!part || !part->chanceCalculated)
    return {};
  // The chance text ("37%") lives on the limb_percent tile, exactly where the screen shows it.
  return UIUtils::GetTileString(part->percentTile);
}

std::vector<ListedTarget> ReadListing() {
  std::vector<ListedTarget> targets;
  for (auto *node = Vats::GetTargetListHead(); node && node->item; node = node->next) {
    auto *target = node->item;
    // Explicitly friendly actors are never offered, even when the engine would let them be selected.
    if (!target->targetRef || target->type == kProjectileTargetType ||
        CombatHandler::IsExplicitlyFriendly(target->targetRef))
      continue;
    ListedTarget listed;
    listed.formId = target->targetRef->GetFormID();
    const char *name = target->targetRef->GetFullName();
    listed.name = name && name[0] ? name : "Unnamed";
    if (target->targetRef->IsActor()) {
      auto *actor = static_cast<Actor *>(target->targetRef);
      listed.health = actor->avOwner.GetActorValueF(eActorVal_Health);
      listed.maxHealth = actor->avOwner.GetPermanentActorValueF(eActorVal_Health);
    }
    for (auto *partNode = &target->bodyParts; partNode && partNode->item; partNode = partNode->next)
      listed.parts.push_back({partNode->item->bodyPartId, LimbNameOf(partNode->item), ChanceOf(partNode->item)});
    if (!listed.parts.empty())
      targets.push_back(std::move(listed));
  }
  return targets;
}

const ListedTarget *FindLiveTarget(const std::vector<ListedTarget> &listing, uint32_t formId) {
  auto found = std::find_if(listing.begin(), listing.end(),
                            [formId](const ListedTarget &target) { return target.formId == formId; });
  return found == listing.end() ? nullptr : &*found;
}

std::string BuildState(Menu *menu) {
  std::ostringstream text;
  text << "## VATS\n- Action points: " << static_cast<int>(Vats::ReadMenuFloat(menu, Vats::kOffsetActionPoints)) << "/"
       << static_cast<int>(Vats::ReadMenuFloat(menu, Vats::kOffsetMaxActionPoints))
       << "\n- Shots queued: " << Vats::GetQueuedShots(menu);
  if (auto *current = Vats::GetCurrentTarget()) {
    const char *name = current->GetFullName();
    text << "\n- Currently selected: " << (name && name[0] ? name : "Unnamed");
  }
  text << "\n\n## Targets";
  for (size_t index = 0; index < g_listing.size(); ++index) {
    const auto &target = g_listing[index];
    text << "\n" << (index + 1) << ". **" << target.name << "**";
    if (target.maxHealth > 0.0f)
      text << " - health " << static_cast<int>(target.health) << "/" << static_cast<int>(target.maxHealth);
    text << "\n   Body parts:";
    for (size_t part = 0; part < target.parts.size(); ++part) {
      text << " " << (part + 1) << "=" << target.parts[part].name;
      text << (target.parts[part].chance.empty() ? " (hit chance shown once selected)"
                                                 : " (" + target.parts[part].chance + " to hit)")
           << (part + 1 < target.parts.size() ? "," : "");
    }
  }
  return text.str();
}

void CloseWindow() {
  if (!g_window)
    return;
  if (g_window->End())
    g_window.reset();
}

void Notify(const std::string &message, bool silent) { NeuroSDK::SendContext(message.c_str(), silent); }

void AbortExecution(const std::string &reason) {
  _WARNING("VATS action stopped: %s", reason.c_str());
  Notify("VATS action stopped: " + reason, false);
  g_execution.reset();
}

void FailOpen(const std::string &reason) {
  _WARNING("VATS open failed: %s", reason.c_str());
  Notify("VATS could not be opened: " + reason, false);
  g_open.reset();
}

bool ClickIfAvailable(Menu *menu, const char *path) {
  auto *control = UIUtils::GetControl(menu, path);
  return control && UIUtils::IsControlAvailable(control) && UIUtils::ClickTile(menu, control);
}

void AdvanceOpen(Menu *menu, bool selecting, float deltaTime) {
  const auto now = Clock::now();
  if (selecting) {
    _MESSAGE("VATS opened");
    g_open.reset();
    return;
  }
  if (!g_open->pressed) {
    bool ready = true;
    if (g_open->target) {
      if (!LookupReference(g_open->target->referenceFormId)) {
        FailOpen(g_open->target->name + " is no longer available.");
        return;
      }
      ready = CombatHandler::AimAtTarget(*g_open->target, deltaTime) || now - g_open->startedAt > kAimTimeout;
    }
    if (ready) {
      PulseControl(ControlCode::VATS_);
      g_open->pressed = true;
      g_open->pressedAt = now;
      _VMESSAGE("VATS key pressed");
    }
    return;
  }
  if (now - g_open->pressedAt > kOpenTimeout)
    FailOpen("it needs an attackable target in view, action points and working controls.");
}

void AdvanceShot(Menu *menu, Clock::time_point now) {
  auto &exec = *g_execution;
  if (now < exec.nextStepAt)
    return;
  switch (exec.phase) {
  case ShotPhase::SelectTarget: {
    auto *current = Vats::GetCurrentTarget();
    if (current && current->GetFormID() == exec.shot.targetFormId) {
      exec.phase = ShotPhase::SelectLimb;
      exec.attempts = 0;
      exec.nextStepAt = now + kStepDelay;
      return;
    }
    // Forward is tried first, then backward, so the target is reached whichever way the engine cycles.
    const int limit = 2 * exec.targetCount + 2;
    if (exec.attempts >= limit) {
      AbortExecution("Could not switch to " + exec.shot.targetName + ".");
      return;
    }
    PulseControl(exec.attempts < exec.targetCount + 1 ? ControlCode::Forward : ControlCode::Backward);
    ++exec.attempts;
    exec.nextStepAt = now + kSwitchDelay;
    return;
  }
  case ShotPhase::SelectLimb: {
    auto *part = Vats::GetCurrentBodyPart(menu);
    if (part && part->bodyPartId == exec.shot.bodyPartId) {
      exec.phase = ShotPhase::Fire;
      exec.nextStepAt = now + kStepDelay;
      return;
    }
    // Keys first (right cycles forward), then the clickable on-screen arrow, each over the whole part list.
    const int keyAttempts = exec.partCount + 1;
    if (exec.attempts >= 2 * keyAttempts) {
      AbortExecution("Could not select " + exec.shot.bodyPartName + " on " + exec.shot.targetName + ".");
      return;
    }
    if (exec.attempts < keyAttempts)
      PulseControl(ControlCode::Right);
    else
      ClickIfAvailable(menu, kRightArrow);
    ++exec.attempts;
    exec.nextStepAt = now + kStepDelay;
    return;
  }
  case ShotPhase::Fire:
    exec.queuedBefore = Vats::GetQueuedShots(menu);
    exec.apBefore = Vats::ReadMenuFloat(menu, Vats::kOffsetActionPoints);
    PulseControl(ControlCode::Attack);
    exec.phase = ShotPhase::Verify;
    exec.deadline = now + kVerifyTimeout;
    exec.nextStepAt = now + kStepDelay;
    return;
  case ShotPhase::Verify:
    if (Vats::GetQueuedShots(menu) > exec.queuedBefore ||
        Vats::ReadMenuFloat(menu, Vats::kOffsetActionPoints) < exec.apBefore - 0.5f) {
      _MESSAGE("VATS queued a shot at %s on %s", exec.shot.bodyPartName.c_str(), exec.shot.targetName.c_str());
      g_execution.reset();
    } else if (now > exec.deadline) {
      AbortExecution("VATS did not queue the shot. You may not have enough action points for it.");
    }
    return;
  }
}

void AdvanceExecution(Menu *menu, bool selecting, bool top) {
  const auto now = Clock::now();
  auto &exec = *g_execution;

  if (exec.kind == ExecKind::Shot) {
    if (!menu || menu != exec.owner || !selecting) {
      AbortExecution("VATS closed or changed while the shot was being queued.");
      return;
    }
    AdvanceShot(menu, now);
    return;
  }

  if (exec.kind == ExecKind::Execute) {
    if (!exec.clicked) {
      if (!menu || menu != exec.owner || !selecting) {
        AbortExecution("VATS closed before the queued shots could be confirmed.");
        return;
      }
      if (!ClickIfAvailable(menu, kAcceptButton)) {
        AbortExecution("The accept control is unavailable. Queue at least one shot first.");
        return;
      }
      exec.clicked = true;
      exec.deadline = now + kPlaybackStartTimeout;
      _VMESSAGE("VATS accept clicked");
      return;
    }
    if (Vats::GetMode() == Vats::kModePlayback || !top) {
      _MESSAGE("VATS attack sequence started");
      g_playback = true;
      g_execution.reset();
    } else if (now > exec.deadline) {
      AbortExecution("VATS did not start the attack sequence.");
    }
    return;
  }

  // Cancel
  if (!exec.clicked) {
    if (!menu || !ClickIfAvailable(menu, kReturnButton)) {
      AbortExecution("The return control is unavailable.");
      return;
    }
    exec.clicked = true;
    exec.deadline = now + kCancelTimeout;
    return;
  }
  if (!top && Vats::GetMode() == Vats::kModeNone) {
    _MESSAGE("VATS cancelled");
    Notify("You left VATS without attacking.", true);
    g_execution.reset();
  } else if (now > exec.deadline) {
    AbortExecution("VATS did not close.");
  }
}

void PublishWindow(Menu *menu) {
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Ended)
    g_window.reset();
  if (g_window && g_window->GetState() == Actions::ActionWindow::State::Closing) {
    CloseWindow();
    return;
  }
  if (g_window || !g_registerRetry.Ready())
    return;
  auto &registry = Actions::ActionRegistry::Get();
  if (registry.HasPendingResult("vats_queue_shot") || registry.HasPendingResult("vats_execute") ||
      registry.HasPendingResult("vats_cancel"))
    return;

  g_listing = ReadListing();
  if (g_listing.empty())
    return; // body parts are built a moment after the menu opens

  const std::string state = BuildState(menu);
  const std::string query =
      "You are in VATS. Queue shots with vats_queue_shot, then run them with vats_execute, or leave with vats_cancel.";
  g_window = std::make_unique<Actions::ActionWindow>();
  _MESSAGE("Opening VATS action window");
  g_window->Add(std::make_unique<Actions::Combat::VatsQueueShotAction>());
  if (Vats::GetQueuedShots(menu) > 0)
    g_window->Add(std::make_unique<Actions::Combat::VatsExecuteAction>());
  g_window->Add(std::make_unique<Actions::Combat::VatsCancelAction>())
      .SetForce(query, state, NeuroSDK::ActionPriority::Medium);
  if (!g_window->Register()) {
    g_registerRetry.Fail();
    if (g_window->GetState() != Actions::ActionWindow::State::Closing)
      g_window.reset();
  }
}

void FinishPlayback() {
  g_playback = false;
  _MESSAGE("VATS attack sequence finished");
  Notify("## VATS finished\nYour VATS attack sequence has ended.\n\n" + CombatHandler::BuildPlayerStatus(), false);
}

} // namespace

void Reset() {
  g_pulses.ReleaseAll();
  if (g_window) {
    g_window->Release();
    g_window.reset();
  }
  g_registerRetry.Reset();
  g_listing.clear();
  g_execution.reset();
  g_open.reset();
  g_playback = false;
  g_active = false;
  g_playbackSince = {};
  g_playbackWatchdogLogged = false;
}

bool IsActive() { return g_active; }

namespace {

bool ProcessFrame(bool unobstructed) {
  auto *menu = Vats::GetMenu();
  const uint32_t mode = Vats::GetMode();
  const bool top = menu && UIUtils::IsTopMenu(menu, Interface::VATS);
  const bool selecting = IsSelecting(menu);
  const bool playbackBlocks = PlaybackBlocks(mode);
  const float deltaTime = CombatHandler::FrameTime();
  g_pulses.Advance(deltaTime);

  if (g_open) {
    AdvanceOpen(menu, selecting, deltaTime);
    return true;
  }
  if (g_execution) {
    AdvanceExecution(menu, selecting, top);
    return true;
  }
  if (g_playback) {
    CloseWindow();
    if (selecting) {
      g_playback = false; // the accept never started a sequence: back to choosing
    } else if (!playbackBlocks && !top) {
      FinishPlayback();
      return false;
    }
    return true;
  }
  if (playbackBlocks) {
    // Playback started without our help (for example a scripted sequence): just stay out of the way.
    CloseWindow();
    return true;
  }
  if (!selecting || !unobstructed) {
    CloseWindow();
    // Only the menu itself blocks here: the mode global is not enough on its own to claim the screen.
    return top;
  }
  PublishWindow(menu);
  return true;
}

} // namespace

bool Process(bool unobstructed) {
  g_active = ProcessFrame(unobstructed);
  return g_active;
}

Actions::PreparedAction PrepareOpen(std::optional<CombatHandler::TargetSelection> target) {
  std::string error;
  if (!GameplayHandler::ValidateGameplayAction(error))
    return Actions::PreparedAction::Failure(std::move(error));
  auto *player = PlayerCharacter::GetSingleton();
  if (IsActive())
    return Actions::PreparedAction::Failure("VATS is already active.");
  if (player->GetDead())
    return Actions::PreparedAction::Failure("You cannot use VATS while dead.");
  if (player->avOwner.GetActorValueF(eActorVal_ActionPoints) < 1.0f)
    return Actions::PreparedAction::Failure(
        "You have no action points left, and VATS needs them. Wait for them to recharge.");
  if (InputUtils::ResolveControl(static_cast<int>(ControlCode::VATS_)) == InputUtils::kNoControl)
    return Actions::PreparedAction::Failure("VATS has no key binding, so it cannot be opened.");

  return Actions::PreparedAction::Success(
      [target = std::move(target)]() {
        g_open = std::make_unique<OpenState>();
        g_open->target = target;
        g_open->startedAt = Clock::now();
        _MESSAGE("VATS opening requested%s", target ? (" toward " + target->name).c_str() : "");
      },
      []() -> std::optional<std::string> {
        std::string revalidationError;
        if (!GameplayHandler::ValidateGameplayAction(revalidationError))
          return revalidationError;
        if (IsActive())
          return "VATS is already active.";
        return std::nullopt;
      });
}

bool ValidateShot(int targetIndex, int bodyPartIndex, ShotRequest &request, std::string &error) {
  auto *menu = Vats::GetMenu();
  if (!IsSelecting(menu) || g_execution) {
    error = "VATS is not ready for another selection.";
    return false;
  }
  if (targetIndex < 1 || targetIndex > static_cast<int>(g_listing.size())) {
    error = "target must be between 1 and " + std::to_string(g_listing.size()) + ".";
    return false;
  }
  const auto &target = g_listing[targetIndex - 1];
  if (bodyPartIndex < 1 || bodyPartIndex > static_cast<int>(target.parts.size())) {
    error = "body_part for " + target.name + " must be between 1 and " + std::to_string(target.parts.size()) + ".";
    return false;
  }
  const auto &part = target.parts[bodyPartIndex - 1];
  request = {target.formId, part.id, target.name, part.name};
  return RevalidateShot(request, error);
}

bool RevalidateShot(const ShotRequest &request, std::string &error) {
  auto *menu = Vats::GetMenu();
  if (!IsSelecting(menu) || g_execution) {
    error = "VATS is not ready for another selection.";
    return false;
  }
  const auto live = ReadListing();
  const auto *target = FindLiveTarget(live, request.targetFormId);
  const bool partExists =
      target && std::any_of(target->parts.begin(), target->parts.end(),
                            [&request](const ListedPart &part) { return part.id == request.bodyPartId; });
  if (!partExists) {
    error = request.targetName + " or its " + request.bodyPartName + " is no longer selectable in VATS.";
    return false;
  }
  return true;
}

void StartShot(const ShotRequest &request) {
  auto *menu = Vats::GetMenu();
  const auto *target = FindLiveTarget(g_listing, request.targetFormId);
  g_execution = std::make_unique<Execution>();
  g_execution->kind = ExecKind::Shot;
  g_execution->owner = menu;
  g_execution->shot = request;
  g_execution->partCount = target ? static_cast<int>(target->parts.size()) : 1;
  g_execution->targetCount = static_cast<int>(std::max<size_t>(g_listing.size(), 1));
  g_execution->nextStepAt = Clock::now();
  _MESSAGE("VATS shot requested: %s on %s", request.bodyPartName.c_str(), request.targetName.c_str());
}

bool ValidateExecute(std::string &error) {
  auto *menu = Vats::GetMenu();
  if (!IsSelecting(menu) || g_execution) {
    error = "VATS is not ready to run the queued shots.";
    return false;
  }
  if (Vats::GetQueuedShots(menu) <= 0) {
    error = "No shots are queued. Queue at least one shot with vats_queue_shot first.";
    return false;
  }
  return true;
}

void StartExecute() {
  g_execution = std::make_unique<Execution>();
  g_execution->kind = ExecKind::Execute;
  g_execution->owner = Vats::GetMenu();
  g_execution->nextStepAt = Clock::now();
}

bool ValidateCancel(std::string &error) {
  if (!IsSelecting(Vats::GetMenu()) || g_execution) {
    error = "VATS is not ready to be cancelled.";
    return false;
  }
  return true;
}

void StartCancel() {
  g_execution = std::make_unique<Execution>();
  g_execution->kind = ExecKind::Cancel;
  g_execution->owner = Vats::GetMenu();
  g_execution->nextStepAt = Clock::now();
}

} // namespace Menus::VatsHandler
