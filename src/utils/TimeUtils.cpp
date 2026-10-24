#include "TimeUtils.hpp"
#include "GameData.h"
#include <algorithm>

namespace TimeUtils {

float FrameSeconds() {
  auto *time = TimeGlobal::Get();
  return time ? std::clamp(time->secondsPassed, 0.0f, kMaxFrameSeconds) : 0.0f;
}

} // namespace TimeUtils
