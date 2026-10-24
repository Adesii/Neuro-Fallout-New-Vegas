#pragma once

#include "Movement/WalkerLogic.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace Walker {

enum class Intent { Move, Interact };
enum class Owner { None, Quest, Object, Exploration, Combat };
// Started/Completed carry their own sentence in `message`. Failed carries a reason (and the blocking object in
// `detail` when known); Progress and Paused carry numbers or a cause. GameplayHandler turns those into text with the
// WalkerLogic::Describe* helpers, so Walker never talks to NeuroSDK itself.
enum class EventType { Completed, Failed, Started, Progress, Paused };

struct Event {
  EventType type = EventType::Failed;
  Owner owner = Owner::None;
  uint32_t targetFormId = 0;
  std::string message;
  WalkerLogic::FailureReason reason = WalkerLogic::FailureReason::Generic;
  std::string description; // what the command was heading for
  std::string detail;      // Failed: the blocking object; Paused: why the walk is on hold
  float walkedDistance = 0.0f;
  float walkSeconds = 0.0f;
  float remainingDistance = 0.0f;
};

// Why the walker is held. NeuroSDK passes Menu or PipBoy; Walker itself detects the rest.
enum class PauseReason { Menu, PipBoy, Controls };

bool Start(uint32_t targetFormId, Intent intent, Owner owner, std::string description);
bool CanInteract(uint32_t targetFormId);
void Process();
// Releases every held input. Called every frame while paused; a command on hold for 30 s gets one silent Paused event.
void Pause(PauseReason reason = PauseReason::Menu);
void Stop();
bool IsActive();
Owner GetOwner();
uint32_t GetTargetFormId();
// What the active command is heading for, as given to Start(); empty when nothing is active.
std::string GetDescription();
// While set, Walker keeps moving the player but leaves yaw and pitch alone (combat owns the aim).
void SetLookSuppressed(bool suppressed);
std::vector<Event> TakeEvents();

} // namespace Walker
