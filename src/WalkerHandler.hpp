#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Walker {

enum class Intent { Move, Interact };
enum class Owner { None, Quest, Object, Exploration, Combat };
enum class EventType { Completed, Failed, Started };

struct Event {
  EventType type = EventType::Failed;
  Owner owner = Owner::None;
  uint32_t targetFormId = 0;
  std::string message;
};

bool Start(uint32_t targetFormId, Intent intent, Owner owner, std::string description);
bool CanInteract(uint32_t targetFormId);
void Process();
void Pause();
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
