#include "Menus/PopupPolicy.hpp"
#include "TestFramework.hpp"

using Menus::PopupPolicy::ChooseAutomaticButton;

TEST("popup policy: single-button popups are always dismissed") {
  CHECK(ChooseAutomaticButton("", "You have been healed.", {"OK"}) == std::optional<size_t>(0));
  CHECK(ChooseAutomaticButton("Notice", "", {"Continue"}) == std::optional<size_t>(0));
}

TEST("popup policy: hardcore prompt is answered No whatever the order or case") {
  CHECK(ChooseAutomaticButton("", "Would you like to play in Hardcore Mode?", {"Yes", "No"}) ==
        std::optional<size_t>(1));
  CHECK(ChooseAutomaticButton("", "Would you like to play in Hardcore Mode?", {"No", "Yes"}) ==
        std::optional<size_t>(0));
  CHECK(ChooseAutomaticButton("HARDCORE MODE", "Choose a difficulty.", {"yes", "no"}) == std::optional<size_t>(1));
}

TEST("popup policy: the real hardcore popup picks No (Recommended), not Yes") {
  const std::string text = "Fallout: New Vegas allows you to play in a new Hardcore mode that greatly increases the "
                           "challenge of the game. Would you like to activate Hardcore mode?";
  CHECK(ChooseAutomaticButton("Hardcore Mode", text, {"No (Recommended)", "Yes"}) == std::optional<size_t>(0));
  CHECK(ChooseAutomaticButton("Hardcore Mode", text, {"Yes", "No (Recommended)"}) == std::optional<size_t>(1));
}

TEST("popup policy: a button label only matches on a whole leading word") {
  CHECK(!ChooseAutomaticButton("Hardcore Mode", "", {"Notice", "Yes"}));
  CHECK(ChooseAutomaticButton("Hardcore Mode", "", {"No, thanks", "Yes"}) == std::optional<size_t>(0));
}

TEST("popup policy: hardcore popup without a No button is left to Neuro") {
  CHECK(!ChooseAutomaticButton("", "Hardcore mode?", {"Accept", "Decline"}));
}

TEST("popup policy: other multi-button popups are left to Neuro") {
  CHECK(!ChooseAutomaticButton("", "Are you sure you want to steal this?", {"Yes", "No"}));
  CHECK(!ChooseAutomaticButton("", "Choose", {"One", "Two", "Three"}));
}

TEST("popup policy: popups with no buttons are left alone") { CHECK(!ChooseAutomaticButton("", "Hardcore", {})); }
