#pragma once

namespace DelayedGuard {

bool Delay(std::string idname, float delaySeconds);

float GetRemainingDelay(std::string idname);

void Reset(std::string idname);

} // namespace DelayedGuard
