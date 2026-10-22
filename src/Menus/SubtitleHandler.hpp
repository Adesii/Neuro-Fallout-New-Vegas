#pragma once

namespace Menus::SubtitleHandler {

void Process();
// Disconnect, load and new game: forget what was seen and drop the unsent batch so nothing stale reaches Neuro.
void Reset();

} // namespace Menus::SubtitleHandler
