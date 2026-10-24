#include "PopupPolicy.hpp"
#include <algorithm>
#include <array>
#include <cctype>

namespace Menus::PopupPolicy {
namespace {

// A popup matches when its title or text contains `textContains` and one of its buttons is labelled `button`,
// optionally followed by more words ("No (Recommended)" matches "No"; "Notice" does not). Both are stored lowercase:
// the comparison lowercases the popup, not the table.
struct KnownPopup {
  std::string_view textContains;
  std::string_view button;
};

constexpr std::array kKnownPopups = {
    // "Hardcore Mode" prompt offered once the tutorial ends, with buttons "No (Recommended)" and "Yes".
    // Hardcore mode is never wanted for these saves.
    KnownPopup{"hardcore mode", "no"},
};

std::string Lower(std::string_view text) {
  std::string lowered(text);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return lowered;
}

bool LabelMatches(const std::string &label, const std::string &wanted) {
  if (!label.starts_with(wanted))
    return false;
  return label.size() == wanted.size() || !std::isalnum(static_cast<unsigned char>(label[wanted.size()]));
}

} // namespace

std::optional<size_t> ChooseAutomaticButton(std::string_view title, std::string_view text,
                                            const std::vector<std::string> &buttonLabels) {
  if (buttonLabels.size() == 1)
    return 0;

  const std::string haystack = Lower(title) + "\n" + Lower(text);
  for (const auto &known : kKnownPopups) {
    if (haystack.find(known.textContains) == std::string::npos)
      continue;
    const std::string wanted(known.button);
    for (size_t index = 0; index < buttonLabels.size(); ++index) {
      if (LabelMatches(Lower(buttonLabels[index]), wanted))
        return index;
    }
  }
  return std::nullopt;
}

} // namespace Menus::PopupPolicy
