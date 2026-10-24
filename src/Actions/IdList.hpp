#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Actions {

// The correction message for a stale or wrong id: "Valid enemy ids are 1, 2, 3. Run query_enemies to refresh the
// list.", or "There are no current enemy ids. Run query_enemies to refresh the list." when there are none. One builder
// for every query action's ids (quests, nearby objects, enemies).
std::string FormatValidIds(std::string_view noun, const std::vector<int> &ids, std::string_view refreshAction);

} // namespace Actions
