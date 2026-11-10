#include "QuestListing.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <utility>

namespace Quests {
namespace {

bool EqualsNoCase(std::string_view left, std::string_view right) {
  return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
         });
}

struct KnownAddOn {
  std::string_view file;
  std::string_view name;
};

// The official add-on masters. File names are the commonly known ones, NOT confirmed from the reference corpus
// (no reference repository names them); check them against the "Quest ... plugin=" log lines in game. A wrong or
// missing entry only changes the label: the quest is then tagged as a mod with its file name.
constexpr std::array<KnownAddOn, 9> kKnownAddOns{{
    {"DeadMoney.esm", "Dead Money"},
    {"HonestHearts.esm", "Honest Hearts"},
    {"OldWorldBlues.esm", "Old World Blues"},
    {"LonesomeRoad.esm", "Lonesome Road"},
    {"GunRunnersArsenal.esm", "Gun Runners' Arsenal"},
    {"CaravanPack.esm", "Courier's Stash"},
    {"ClassicPack.esm", "Courier's Stash"},
    {"MercenaryPack.esm", "Courier's Stash"},
    {"TribalPack.esm", "Courier's Stash"},
}};

// Empty on purpose: see the header. Fill with verified editor IDs only.
constexpr std::array<std::string_view, 0> kMainQuestEditorIds{};

} // namespace

std::span<const std::string_view> MainQuestEditorIds() { return kMainQuestEditorIds; }

Category Classify(std::string_view pluginFile, std::string_view editorId,
                  std::span<const std::string_view> mainEditorIds) {
  if (pluginFile.empty() || EqualsNoCase(pluginFile, kBaseGameFile)) {
    if (mainEditorIds.empty())
      return {CategoryKind::BaseGame, "base game"};
    const bool main = !editorId.empty() && std::any_of(mainEditorIds.begin(), mainEditorIds.end(),
                                                       [&](std::string_view id) { return EqualsNoCase(id, editorId); });
    return main ? Category{CategoryKind::Main, "main story"} : Category{CategoryKind::Side, "side quest"};
  }
  for (const auto &addOn : kKnownAddOns) {
    if (EqualsNoCase(pluginFile, addOn.file))
      return {CategoryKind::Dlc, "DLC: " + std::string(addOn.name)};
  }
  std::string_view stem = pluginFile;
  if (const auto dot = stem.rfind('.'); dot != std::string_view::npos && dot > 0)
    stem = stem.substr(0, dot);
  return {CategoryKind::Mod, "mod: " + std::string(stem)};
}

int CategoryRank(CategoryKind kind) { return kind == CategoryKind::Main ? 0 : 1; }

bool ListedBefore(const OrderKey &left, const OrderKey &right) {
  if (left.rank != right.rank)
    return left.rank < right.rank;
  if (left.priority != right.priority)
    return left.priority > right.priority;
  if (left.questFormId != right.questFormId)
    return left.questFormId < right.questFormId;
  if (left.objectiveId != right.objectiveId)
    return left.objectiveId < right.objectiveId;
  return left.targetFormId < right.targetFormId;
}

std::string SelectedQuestLine(const std::optional<SelectedQuest> &selected) {
  if (!selected)
    return {};
  std::string line = "You already have quest " + selected->name + " selected";
  if (!selected->objective.empty())
    line += " (current objective: " + selected->objective + ")";
  line += ".";
  return line;
}

namespace {

bool SameObjective(const Row &left, const Row &right) {
  return left.key.questFormId == right.key.questFormId && left.key.objectiveId == right.key.objectiveId;
}

} // namespace

std::string BuildQuestListing(const std::optional<SelectedQuest> &selected, std::vector<Row> rows) {
  std::stable_sort(rows.begin(), rows.end(),
                   [](const Row &left, const Row &right) { return ListedBefore(left.key, right.key); });

  std::string text;
  if (selected)
    text = SelectedQuestLine(selected) + "\n\n";
  text += "## Current quest objectives";
  if (rows.empty()) {
    text += "\n- None.";
    return text;
  }

  for (size_t index = 0; index < rows.size();) {
    const Row &first = rows[index];
    // One objective: rows[index, end) share quest and objective.
    size_t end = index + 1;
    while (end < rows.size() && SameObjective(rows[end], first))
      ++end;

    if (index == 0 || rows[index - 1].key.questFormId != first.key.questFormId) {
      text += "\n\n### " + first.questName;
      const bool isSelected = selected && selected->questFormId == first.key.questFormId;
      if (!first.category.empty() && isSelected)
        text += " (" + first.category + ", selected)";
      else if (!first.category.empty())
        text += " (" + first.category + ")";
      else if (isSelected)
        text += " (selected)";
    }

    const size_t targets = static_cast<size_t>(
        std::count_if(rows.begin() + index, rows.begin() + end, [](const Row &row) { return row.id != 0; }));
    if (targets == 0) {
      text += "\n- " + first.objectiveText + " - no map marker, so it has no id to select or follow";
    } else {
      size_t place = 0;
      for (size_t row = index; row < end; ++row) {
        if (rows[row].id == 0)
          continue;
        ++place;
        text += "\n- `" + std::to_string(rows[row].id) + "` - " + rows[row].objectiveText;
        if (targets > 1)
          text += " (place " + std::to_string(place) + " of " + std::to_string(targets) + ")";
        text += " - " + rows[row].location;
      }
    }
    index = end;
  }
  return text;
}

} // namespace Quests
