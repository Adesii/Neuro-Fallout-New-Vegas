#include "IdList.hpp"

namespace Actions {

std::string FormatValidIds(std::string_view noun, const std::vector<int> &ids, std::string_view refreshAction) {
  std::string text;
  if (ids.empty()) {
    text = "There are no current " + std::string(noun) + " ids.";
  } else {
    text = "Valid " + std::string(noun) + " ids are ";
    for (size_t index = 0; index < ids.size(); ++index) {
      if (index)
        text += ", ";
      text += std::to_string(ids[index]);
    }
    text += ".";
  }
  return text + " Run " + std::string(refreshAction) + " to refresh the list.";
}

} // namespace Actions
