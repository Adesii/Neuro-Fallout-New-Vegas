#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The engine-free text of the `query_quests` result (todos 41 and 04). GameplayHandler reads the quest log and the
// active quest and feeds them in. Host-tested in tests/QuestListingTests.cpp.
namespace Quests {

// What select_quest last chose inside the selected quest. The quest itself is SelectedQuest::questFormId;
// GameplayHandler only fills this when the choice belongs to that quest.
struct Chosen {
  uint32_t objectiveId = 0;
  uint32_t targetFormId = 0;
};

// The quest the player already has selected (`player->activeQuest`). `objective` is left empty when it is not known
// which of the quest's objectives is meant; the line then names only the quest. `chosen` (what select_quest picked,
// if anything) decides which list entry is tagged "(selected)"; `routeRunning` is whether do_current_quest is following
// the quest right now (the quest can stay selected while the route is off, e.g. after move_to_object).
struct SelectedQuest {
  std::string name;
  std::string objective;
  uint32_t questFormId = 0;
  std::optional<Chosen> chosen;
  bool routeRunning = false;
};

// Quest categories (todo 04). FNV quest data has no main/side field: TESQuest holds flags, priority and the editor ID
// (JG/JIP GameForms.h, TESQuest +0x3C..+0x64) and nothing else that says "main story". So:
// - DLC (and other add-ons) are told apart by the plugin that defines the quest (the form id's load-order index).
// - Main vs side in the base game needs a list of main-quest editor IDs (MainQuestEditorIds). That list could not be
//   sourced in Cloud and is empty, so base-game quests are tagged "base game" until it is filled from in-game data.
enum class CategoryKind { Main, Side, BaseGame, Dlc, Mod };

struct Category {
  CategoryKind kind = CategoryKind::BaseGame;
  std::string label; // the tag shown in the quest heading, e.g. "main story", "DLC: Dead Money"
};

// The base game's master file name (compared without case).
inline constexpr std::string_view kBaseGameFile = "FalloutNV.esm";

// The base game's main-quest editor IDs. Empty until verified in game (see todo 04); while empty, no quest is "main".
std::span<const std::string_view> MainQuestEditorIds();

// `pluginFile` is the file that defines the quest (empty if unknown: treated as the base game). `mainEditorIds` is
// MainQuestEditorIds() outside the tests. Known DLC files get the DLC's name; any other plugin is a mod, labelled with
// its file name without extension.
Category Classify(std::string_view pluginFile, std::string_view editorId,
                  std::span<const std::string_view> mainEditorIds);

// Main story first (rank 0), everything else after it (rank 1), as the designer chose: DLC and side quests are not
// pushed below each other, they keep their own priority order.
int CategoryRank(CategoryKind kind);

// Where a row sits in the listing: main story first, then higher quest priority, then by quest, objective and target.
// The query_quests ids are numbered in this order, so ids read top to bottom.
struct OrderKey {
  int rank = 1;
  int priority = 0;
  uint32_t questFormId = 0;
  uint32_t objectiveId = 0;
  uint32_t targetFormId = 0;
};
bool ListedBefore(const OrderKey &left, const OrderKey &right);

// One line of the listing: one target of a displayed objective, with its select_quest id, or (id 0) a displayed
// objective without any usable target (no map marker), which is listed but cannot be selected.
struct Row {
  OrderKey key;
  std::string questName;
  std::string category; // Category::label of the quest; empty: no tag
  std::string objectiveText;
  int id = 0;
  std::string location;  // where the target is; unused when id is 0
  bool selected = false; // the entry select_quest chose; set by MarkSelectedRows, shown as "(selected)"
};

// Tags the rows select_quest chose: the exact target if it is still listed, otherwise every target of the chosen
// objective. Nothing is tagged without a selected quest or a choice (the quest was picked in the Pip-Boy). Rows without
// an id (no map marker) are never tagged. Clears the tag on every other row.
void MarkSelectedRows(const std::optional<SelectedQuest> &selected, std::vector<Row> &rows);

// "You already have quest X selected (current objective: Y)." plus whether do_current_quest is following it, when
// `selected` is set, otherwise nothing.
std::string SelectedQuestLine(const std::optional<SelectedQuest> &selected);

// The whole result: the selected-quest line (if any) at the top, then "## Current quest objectives" with one
// "### <quest> (<category>, selected)" heading per quest and its objectives below it. An objective with several targets
// gets one line per target, numbered "place k of n"; one without targets says it has no map marker. The entry
// select_quest chose reads "- `3` (selected) - ...". `rows` is sorted here with ListedBefore (stable), so markerless
// rows can simply be appended to the catalog rows.
std::string BuildQuestListing(const std::optional<SelectedQuest> &selected, std::vector<Row> rows);

} // namespace Quests
