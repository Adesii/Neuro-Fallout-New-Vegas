#include "Greeting.hpp"

namespace Context {

std::string BuildGreeting(const std::string &location) {
  std::string text =
      "## Welcome to the Mojave\n"
      "You are playing Fallout: New Vegas, an open-world post-apocalyptic RPG, as the player character (the Courier). "
      "You are the one playing: everything that happens is yours to decide. Try to have a fun adventure.\n"
      "To see what is going on use query_quests (quests and where their objectives are), query_nearby (people and "
      "objects around you) and query_enemies (who could fight you). Then move, explore, fight or interact with the "
      "actions you are offered; menus and conversations offer their own actions when they open.";
  if (!location.empty())
    text += "\n" + location;
  return text;
}

} // namespace Context
