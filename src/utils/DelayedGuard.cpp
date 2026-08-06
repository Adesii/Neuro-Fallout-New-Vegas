#include "DelayedGuard.hpp"
#include "GameData.h"
#include <unordered_map>

namespace DelayedGuard {

std::unordered_map<std::string, float> delayMap;
std::unordered_map<std::string, float> countMap;

// Returns true if the delay has expired, false if still in delay period
// TODO: Figure out if TickCount is a good measure for this. or if tracking out own seconds passed is better.
// Since SecondsPassed is the delta Time
bool Delay(std::string idname, float delaySeconds) {
  if (delayMap.find(idname) == delayMap.end()) {
    delayMap[idname] = delaySeconds;
    countMap[idname] = 0.0f;
    return false;
  } else {
    float currentTime = countMap[idname] + TimeGlobal::Get()->secondsPassed;
    if (currentTime >= delayMap[idname]) {
      return true;
    } else {
      countMap[idname] = currentTime;
      return false;
    }
  }
}
float GetRemainingDelay(std::string idname) {
  if (delayMap.find(idname) == delayMap.end()) {
    return 0.0f;
  } else {
    float currentTime = countMap[idname];
    float remainingTime = delayMap[idname] - currentTime;
    return remainingTime > 0.0f ? remainingTime : 0.0f;
  }
}

void Reset(std::string idname) {
  delayMap.erase(idname);
  countMap.erase(idname);
}

} // namespace DelayedGuard
