#pragma once

#include <string>

namespace Context {

// The short orientation Neuro gets on connect, after every load or new game, and every ten minutes: the game, her
// role and the discovery actions. `location` is the current place line (todo 28) and is left out when empty. It is
// meant to be sent silent; after a reconnect it leads the non-silent status message instead.
std::string BuildGreeting(const std::string &location);

} // namespace Context
