#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Walker {

enum class Intent { Move, Interact };
enum class Owner { None, Quest, Object, Exploration };
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
std::vector<Event> TakeEvents();

} // namespace Walker
