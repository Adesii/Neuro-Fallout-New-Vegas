#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Menus::PopupPolicy {

// Index of the button the plugin presses on its own, or nullopt when Neuro should decide.
// - A popup with a single button is only informational and is always dismissed.
// - Known multi-button popups are answered with a fixed button (see kKnownPopups in PopupPolicy.cpp).
// Matching ignores case and looks at the title and the text.
std::optional<size_t> ChooseAutomaticButton(std::string_view title, std::string_view text,
                                            const std::vector<std::string> &buttonLabels);

} // namespace Menus::PopupPolicy
