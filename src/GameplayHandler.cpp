#include "GameplayHandler.hpp"
#include "Actions/Gameplay/DoCurrentQuestAction.hpp"
#include "Actions/Gameplay/ExploreAction.hpp"
#include "Actions/Gameplay/QueryNearbyAction.hpp"
#include "Actions/Gameplay/QueryQuestsAction.hpp"
#include "Actions/Gameplay/SelectQuestAction.hpp"
#include "Actions/Gameplay/StopMoveAction.hpp"
#include "Actions/Gameplay/TargetObjectAction.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "Actions/PipBoy/OpenInventoryAction.hpp"
#include "CombatHandler.hpp"
#include "Context/Distance.hpp"
#include "Context/Greeting.hpp"
#include "Context/Location.hpp"
#include "Context/ObjectPriority.hpp"
#include "GameData.h"
#include "GameExtraData.h"
#include "GameObjects.h"
#include "Menus/PipBoyHandler.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "decoding.h"
#include "nvse/GameForms.h"
#include "utils/WorldUtils.hpp"
#include "utils/math.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace GameplayHandler {
namespace {

using WorldUtils::Distance3D;
using WorldUtils::LookupReference;

constexpr float kNearbyRadius = 2500.0f;
constexpr float kExploreRadius = 5000.0f;
constexpr float kExploreMinimumDistance = 300.0f;
constexpr size_t kNearbyLimit = 20;
constexpr size_t kRecentExplorationLimit = 8;
constexpr float kQuestResolveTimeout = 10.0f;
constexpr auto kQuestObservationInterval = std::chrono::milliseconds(500);
constexpr auto kPlaceObservationInterval = std::chrono::milliseconds(500);

// TESQuest::flags bits and the BGSQuestObjective::status display mask.
constexpr uint8_t kQuestFlagCompleted = 0x02;
constexpr uint8_t kQuestFlagFailed = 0x40;
constexpr uint8_t kQuestFlagFinished = kQuestFlagCompleted | kQuestFlagFailed;
constexpr uint32_t kObjectiveDisplayMask = 3;

struct QuestEntry : QuestSelection {
  int id = 0;
  int priority = 0;
  float distance = 0.0f;
  bool sameTravelSpace = false;
  // Only meaningful when the target is in another area: the door to walk through, how far it is, and where the
  // objective itself is. doorDistance stays negative when the door is not in the player's own area.
  std::string doorName;
  float doorDistance = -1.0f;
  bool hasDoor = false;
  std::string destinationName;
};

struct ObjectEntry : ObjectSelection {
  int id = 0;
  float distance = 0.0f;
  Objects::Category category = Objects::Category::None;
  bool questTarget = false;
};

struct ObjectiveObservation {
  uint32_t status = 0;
  uint8_t questFlags = 0;
  std::string description;
};

Actions::PersistentActionSet g_worldActions;
Actions::PersistentActionSet g_questActions;
// Walker commands pause across menus rather than ending, so stopping one stays possible while a menu is open.
Actions::PersistentActionSet g_moveActions;
bool g_actionsBuilt = false;
bool g_ready = false;
bool g_gameplayBlocked = true;
std::vector<QuestEntry> g_quests;
std::vector<ObjectEntry> g_objects;
std::optional<QuestSelection> g_currentQuestRoute;
// What select_quest last chose: do_current_quest follows this objective, not merely the first one of the quest.
std::optional<QuestSelection> g_selectedObjective;
uint32_t g_completedQuestStep = 0;
float g_questResolveTime = 0.0f;
std::vector<uint32_t> g_recentExploration;
uint32_t g_randomState = 0x6D2B79F5;
bool g_observationInitialized = false;
std::chrono::steady_clock::time_point g_nextQuestObservation{};
std::unordered_map<uint64_t, ObjectiveObservation> g_objectiveObservations;
std::unordered_map<uint32_t, uint8_t> g_questFlags;
Location::PlaceTracker g_place;
std::chrono::steady_clock::time_point g_nextPlaceObservation{};
std::chrono::steady_clock::time_point g_lastPlaceObservation{};

void BuildActions() {
  if (g_actionsBuilt)
    return;
  g_questActions.Add(std::make_unique<Actions::Gameplay::QueryQuestsAction>())
      .Add(std::make_unique<Actions::Gameplay::SelectQuestAction>());
  g_worldActions.Add(std::make_unique<Actions::Gameplay::DoCurrentQuestAction>())
      .Add(std::make_unique<Actions::Gameplay::QueryNearbyAction>())
      .Add(std::make_unique<Actions::Gameplay::TargetObjectAction>(false))
      .Add(std::make_unique<Actions::Gameplay::TargetObjectAction>(true))
      .Add(std::make_unique<Actions::Gameplay::ExploreAction>())
      .Add(std::make_unique<Actions::PipBoy::OpenInventoryAction>());
  g_moveActions.Add(std::make_unique<Actions::Gameplay::StopMoveAction>());
  g_actionsBuilt = true;
}

TESQuest *LookupQuest(uint32_t formId) {
  auto *form = TESForm::GetFormByNumericID(formId);
  return form && form->eFormType == _FormType::TESQuest ? static_cast<TESQuest *>(form) : nullptr;
}

std::string QuestName(TESQuest *quest) {
  const char *name = quest ? quest->strFullName.c_str() : nullptr;
  if (name && name[0])
    return name;
  return quest ? "Quest " + std::to_string(quest->GetFormID()) : "Unknown quest";
}

std::string ObjectiveText(BGSQuestObjective *objective) {
  const char *text = objective ? objective->displayText.c_str() : nullptr;
  return text && text[0] ? text : "Unnamed objective";
}

bool IsInSameTravelSpace(TESObjectREFR *a, TESObjectREFR *b) {
  if (!a || !b)
    return false;
  auto *aCell = a->GetParentCell();
  auto *bCell = b->GetParentCell();
  if (!aCell || !bCell)
    return false;
  return aCell == bCell || (aCell->worldSpace && aCell->worldSpace == bCell->worldSpace);
}

std::string DoorLabel(TESObjectREFR *door) {
  const char *name = door ? door->GetFullName() : nullptr;
  return name && name[0] ? std::string("the door \"") + name + "\"" : "a door";
}

// The route's first step is a door whenever the objective lies in another area; the door is not the objective.
bool IsDoorStep(uint32_t stepFormId, const QuestSelection &route) { return stepFormId != route.targetFormId; }

// What Walker is told it is heading for. Naming the objective for a door step made Neuro believe the objective
// itself had been reached.
std::string StepDescription(TESObjectREFR *step, const QuestSelection &route) {
  if (!step || !IsDoorStep(step->GetFormID(), route))
    return route.description;
  return DoorLabel(step) + " that leads toward " + route.description;
}

// The door the engine's quest route says to take first to reach a target in another area.
TESObjectREFR *FindEntryDoor(BGSQuestObjective::Target *target) {
  if (!target || target->data.teleportLinks.IsEmpty() || !target->data.teleportLinks.pBuffer)
    return nullptr;
  auto *door = target->data.teleportLinks.GetAt(0).door;
  return door && !door->GetDeleted() ? door : nullptr;
}

std::string PlaceName(TESObjectREFR *ref) {
  auto *cell = ref ? ref->GetParentCell() : nullptr;
  const char *name = cell ? cell->fullName.GetFullName() : nullptr;
  return name ? name : "";
}

// Map markers the player can see on the world map (the Visible flag: MarkerData flags bit 0, JG MapMarkerData.hpp and
// JIP-LN-NVSE ExtraMapMarker). A marker is a reference to the base form 0x10 (JIP-LN-NVSE ln_fn_map_marker.h) carrying
// ExtraMapMarker; they may live in the grid cells or in the worldspace's persistent cell, so both are read.
// Which of the two actually holds them in FNV is unverified.
void CollectMarkers(PlayerCharacter *player, TESObjectCELL *cell, std::vector<Location::Marker> &markers) {
  std::unordered_set<uint32_t> seen;
  auto consider = [&](TESObjectREFR *ref) {
    if (!ref || !ref->baseForm || ref->baseForm->GetFormID() != 0x10 || ref->GetDeleted() ||
        !seen.insert(ref->GetFormID()).second)
      return;
    auto *extra = ref->extraDataList.GetExtraData<ExtraMapMarker>();
    auto *data = extra ? extra->pData : nullptr;
    if (!data || !data->IsVisible())
      return;
    const char *name = data->kLocationName.GetFullName();
    if (!name || !name[0])
      return;
    markers.push_back({name, Distance3D(player->GetPos(), ref->GetPos())});
  };
  WorldUtils::ForEachLoadedReference([&](TESObjectREFR *ref, TESObjectCELL *) { consider(ref); });
  auto *worldSpace = cell ? cell->GetWorldSpace() : nullptr;
  auto *persistent = worldSpace ? worldSpace->pPersistentCell : nullptr;
  for (auto *node = persistent ? persistent->objectList.GetHead() : nullptr; node; node = node->GetNext())
    consider(node->GetItem());
}

// Where the player is, as the engine-free place describer wants it. The marker walk only happens outdoors in a cell
// with no name of its own.
std::optional<Location::PlaceInput> ReadPlace() {
  auto *player = PlayerCharacter::GetSingleton();
  auto *cell = player ? player->GetParentCell() : nullptr;
  if (!cell)
    return std::nullopt;
  Location::PlaceInput input;
  input.interior = cell->IsInterior();
  auto *worldSpace = cell->GetWorldSpace();
  input.spaceId = input.interior || !worldSpace ? cell->GetFormID() : worldSpace->GetFormID();
  const char *name = cell->fullName.GetFullName();
  input.cellName = name ? name : "";
  if (input.cellName.empty() && !input.interior)
    CollectMarkers(player, cell, input.markers);
  return input;
}

BGSQuestObjective *FindObjective(uint32_t questFormId, uint32_t objectiveId) {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return nullptr;
  for (auto iter = player->questObjectiveList.Begin(); !iter.End(); ++iter) {
    auto *objective = iter.Get();
    if (objective && objective->quest && objective->quest->GetFormID() == questFormId &&
        objective->objectiveId == objectiveId)
      return objective;
  }
  return nullptr;
}

// A target the objective currently points at: it exists, is enabled and its conditions hold.
bool IsUsableTarget(BGSQuestObjective::Target *target) {
  if (!target || !target->target || target->target->GetDeleted() || target->target->GetDisabled())
    return false;
  bool conditionResult = false;
  return target->conditions.Empty() || target->conditions.Evaluate(target->target, nullptr, &conditionResult, false);
}

// One thing Walker can head for to make progress on an objective: the target itself when it is in the player's area,
// otherwise the door that leads toward it.
struct QuestStep {
  BGSQuestObjective::Target *target = nullptr;
  TESObjectREFR *step = nullptr;
  bool sameSpace = false;
  float distance = 0.0f;
};

// Every usable target of the objective that has a way to it, in the order to try them: targets in the player's area
// first (closest first), then targets behind a door. A target in another area whose door is unknown is skipped, not a
// reason to stop looking. `preferredTargetId` restricts the list to that one target.
std::vector<QuestStep> CollectQuestSteps(BGSQuestObjective *objective, uint32_t preferredTargetId) {
  std::vector<QuestStep> steps;
  auto *player = PlayerCharacter::GetSingleton();
  if (!player || !objective)
    return steps;
  for (auto iter = objective->targets.Begin(); !iter.End(); ++iter) {
    auto *target = iter.Get();
    if (!IsUsableTarget(target) || (preferredTargetId && target->target->GetFormID() != preferredTargetId))
      continue;
    if (IsInSameTravelSpace(player, target->target)) {
      steps.push_back({target, target->target, true, Distance3D(player->GetPos(), target->target->GetPos())});
    } else if (auto *door = FindEntryDoor(target)) {
      steps.push_back({target, door, false, 0.0f});
    }
  }
  std::stable_sort(steps.begin(), steps.end(), [](const QuestStep &left, const QuestStep &right) {
    if (left.sameSpace != right.sameSpace)
      return left.sameSpace;
    return left.distance < right.distance;
  });
  return steps;
}

TESObjectREFR *ResolveQuestTarget(BGSQuestObjective *objective, uint32_t preferredTargetId) {
  const auto steps = CollectQuestSteps(objective, preferredTargetId);
  return steps.empty() ? nullptr : steps.front().step;
}

BGSQuestObjective::Target *FindObjectiveTarget(BGSQuestObjective *objective, uint32_t targetFormId) {
  if (!objective)
    return nullptr;
  for (auto iter = objective->targets.Begin(); !iter.End(); ++iter) {
    auto *target = iter.Get();
    if (!target || !target->target || target->target->GetFormID() != targetFormId)
      continue;
    bool conditionResult = false;
    if (!target->conditions.Empty() && !target->conditions.Evaluate(target->target, nullptr, &conditionResult, false))
      return nullptr;
    return target;
  }
  return nullptr;
}

void ActivateQuest(TESQuest *quest) {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player || !quest || player->activeQuest == quest)
    return;
  player->activeQuest = quest;
  // FNV rebuilds tracked objective and teleport-link lists through this routine.
  // Proven by JIP-LN-NVSE/functions_jip/jip_fn_quest.h (GPL-3.0) and FalloutNVAccess (MIT).
  ThisCall<void>(0x60F110, quest, &player->questTargetList, &player->questObjectiveList);
}

// References the active quest objectives point at, and the doors on the way to targets in other areas.
std::unordered_set<uint32_t> CollectQuestTargetIds() {
  std::unordered_set<uint32_t> ids;
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return ids;
  for (auto iter = player->questObjectiveList.Begin(); !iter.End(); ++iter) {
    auto *objective = iter.Get();
    if (!objective || !objective->quest ||
        (objective->status & kObjectiveDisplayMask) != BGSQuestObjective::eQObjStatus_displayed ||
        (objective->quest->flags & kQuestFlagFinished))
      continue;
    for (auto targetIter = objective->targets.Begin(); !targetIter.End(); ++targetIter) {
      auto *target = targetIter.Get();
      if (!IsUsableTarget(target))
        continue;
      ids.insert(target->target->GetFormID());
      if (auto *door = FindEntryDoor(target))
        ids.insert(door->GetFormID());
    }
  }
  return ids;
}

std::vector<ObjectEntry> ScanObjects(float radius, const std::unordered_set<uint32_t> &questTargets = {}) {
  std::vector<ObjectEntry> objects;
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return objects;

  WorldUtils::ForEachLoadedReference([&](TESObjectREFR *ref, TESObjectCELL *cell) {
    if (ref == player)
      return;
    const char *name = ref->GetFullName();
    if (!name || !name[0])
      return;
    const Objects::Classification classification = WorldUtils::ClassifyObject(ref);
    if (classification.category == Objects::Category::None || !classification.interactable)
      return;
    const float distance = Distance3D(player->GetPos(), ref->GetPos());
    if (!std::isfinite(distance) || distance > radius)
      return;
    objects.push_back({{ref->GetFormID(), cell->GetFormID(), name},
                       0,
                       distance,
                       classification.category,
                       questTargets.contains(ref->GetFormID())});
  });

  std::sort(objects.begin(), objects.end(), [](const ObjectEntry &left, const ObjectEntry &right) {
    if (left.distance != right.distance)
      return left.distance < right.distance;
    return left.referenceFormId < right.referenceFormId;
  });
  return objects;
}

void BuildQuestCatalog() {
  g_quests.clear();
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return;

  for (auto objectiveIter = player->questObjectiveList.Begin(); !objectiveIter.End(); ++objectiveIter) {
    auto *objective = objectiveIter.Get();
    if (!objective || !objective->quest ||
        (objective->status & kObjectiveDisplayMask) != BGSQuestObjective::eQObjStatus_displayed ||
        (objective->quest->flags & kQuestFlagFinished))
      continue;
    const std::string description = QuestName(objective->quest) + ": " + ObjectiveText(objective);
    for (auto targetIter = objective->targets.Begin(); !targetIter.End(); ++targetIter) {
      auto *target = targetIter.Get();
      if (!IsUsableTarget(target))
        continue;
      const bool sameSpace = IsInSameTravelSpace(player, target->target);
      const float distance = sameSpace ? Distance3D(player->GetPos(), target->target->GetPos()) : 0.0f;
      QuestEntry entry{
          {objective->quest->GetFormID(), objective->objectiveId, target->target->GetFormID(), description},
          0,
          objective->quest->priority,
          distance,
          sameSpace};
      if (!sameSpace) {
        entry.destinationName = PlaceName(target->target);
        if (auto *door = FindEntryDoor(target)) {
          entry.hasDoor = true;
          const char *doorName = door->GetFullName();
          entry.doorName = doorName ? doorName : "";
          if (IsInSameTravelSpace(player, door))
            entry.doorDistance = Distance3D(player->GetPos(), door->GetPos());
        }
      }
      g_quests.push_back(std::move(entry));
    }
  }

  std::sort(g_quests.begin(), g_quests.end(), [](const QuestEntry &left, const QuestEntry &right) {
    if (left.priority != right.priority)
      return left.priority > right.priority;
    if (left.questFormId != right.questFormId)
      return left.questFormId < right.questFormId;
    if (left.objectiveId != right.objectiveId)
      return left.objectiveId < right.objectiveId;
    return left.targetFormId < right.targetFormId;
  });
  for (size_t index = 0; index < g_quests.size(); ++index)
    g_quests[index].id = static_cast<int>(index + 1);
}

// Says where the objective is and what it takes to get there. Distances are straight-line; for another area the
// distance is to the door, since the objective itself is somewhere beyond it.
std::string DescribeQuestLocation(const QuestEntry &entry) {
  std::ostringstream text;
  if (entry.sameTravelSpace) {
    text << "in your current area, " << Units::FormatDistance(entry.distance) << " away";
    return text.str();
  }
  text << "NOT in your current area";
  if (!entry.destinationName.empty())
    text << " (it is in " << entry.destinationName << ")";
  if (!entry.hasDoor) {
    text << ": you have to go to another area to reach it, but the door to use is not known";
    return text.str();
  }
  text << ": you have to enter a door to get there. The door"
       << (entry.doorName.empty() ? std::string() : " \"" + entry.doorName + "\"");
  if (entry.doorDistance >= 0.0f)
    text << " is " << Units::FormatDistance(entry.doorDistance) << " away from you";
  else
    text << " is not in your current area, so you need to travel to another area first";
  text << "; the objective itself is further, beyond it";
  return text.str();
}

std::string ValidQuestIds() {
  if (g_quests.empty())
    return "There are no currently selectable quest targets. Run query_quests to refresh the list.";
  std::ostringstream stream;
  stream << "Valid quest ids are ";
  for (size_t index = 0; index < g_quests.size(); ++index) {
    if (index)
      stream << ", ";
    stream << g_quests[index].id;
  }
  stream << ".";
  return stream.str();
}

std::string ValidObjectIds() {
  if (g_objects.empty())
    return "There are no current nearby-object ids. Run query_nearby to refresh the list.";
  std::ostringstream stream;
  stream << "Valid nearby object ids are ";
  for (size_t index = 0; index < g_objects.size(); ++index) {
    if (index)
      stream << ", ";
    stream << g_objects[index].id;
  }
  stream << ".";
  return stream.str();
}

void ObserveQuests() {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return;
  std::unordered_map<uint64_t, ObjectiveObservation> currentObjectives;
  std::unordered_map<uint32_t, uint8_t> currentQuestFlags;
  std::vector<std::string> notifications;

  for (auto iter = player->questObjectiveList.Begin(); !iter.End(); ++iter) {
    auto *objective = iter.Get();
    if (!objective || !objective->quest)
      continue;
    const uint32_t questId = objective->quest->GetFormID();
    const uint64_t key = (static_cast<uint64_t>(questId) << 32) | objective->objectiveId;
    ObjectiveObservation observation{objective->status, objective->quest->flags,
                                     QuestName(objective->quest) + ": " + ObjectiveText(objective)};
    currentObjectives.emplace(key, observation);
    currentQuestFlags[questId] = objective->quest->flags;

    if (!g_observationInitialized)
      continue;
    auto previous = g_objectiveObservations.find(key);
    if (previous == g_objectiveObservations.end()) {
      if ((observation.status & kObjectiveDisplayMask) == BGSQuestObjective::eQObjStatus_displayed)
        notifications.push_back("New quest objective: " + observation.description);
    } else if (!(previous->second.status & BGSQuestObjective::eQObjStatus_completed) &&
               (observation.status & BGSQuestObjective::eQObjStatus_completed)) {
      notifications.push_back("Quest objective completed: " + observation.description);
    }
  }

  if (g_observationInitialized) {
    auto reportFlagChange = [&](uint32_t questId, uint8_t previous, uint8_t flags) {
      auto *quest = LookupQuest(questId);
      if (!(previous & kQuestFlagCompleted) && (flags & kQuestFlagCompleted)) {
        notifications.push_back("Quest completed: " + QuestName(quest));
        _MESSAGE("Quest completed: %s (%08X)", QuestName(quest).c_str(), questId);
      }
      if (!(previous & kQuestFlagFailed) && (flags & kQuestFlagFailed)) {
        notifications.push_back("Quest failed: " + QuestName(quest));
        _MESSAGE("Quest failed: %s (%08X)", QuestName(quest).c_str(), questId);
      }
    };
    for (const auto &[questId, flags] : currentQuestFlags) {
      const auto previous = g_questFlags.find(questId);
      reportFlagChange(questId, previous != g_questFlags.end() ? previous->second : flags, flags);
    }
    // The engine may drop a finished quest's objectives together with the quest; its form still has the final flags.
    for (const auto &[questId, previousFlags] : g_questFlags) {
      if (currentQuestFlags.contains(questId))
        continue;
      auto *quest = LookupQuest(questId);
      _MESSAGE("Quest %08X left the objective list (flags %02X -> %02X)", questId, previousFlags,
               quest ? quest->flags : 0);
      if (quest)
        reportFlagChange(questId, previousFlags, quest->flags);
    }
  }

  g_objectiveObservations = std::move(currentObjectives);
  g_questFlags = std::move(currentQuestFlags);
  g_observationInitialized = true;
  if (!notifications.empty()) {
    std::ostringstream context;
    context << "## Quest updates";
    for (const auto &notification : notifications)
      context << "\n- " << notification;
    NeuroSDK::SendContext(context.str().c_str(), false);
  }
}

void ClearCurrentQuestRoute() {
  g_currentQuestRoute.reset();
  g_completedQuestStep = 0;
  g_questResolveTime = 0.0f;
}

void ProcessWalkerEvents() {
  for (auto &event : Walker::TakeEvents()) {
    // CombatHandler closes in on melee targets constantly; only a failed approach is worth telling Neuro about.
    if (event.owner == Walker::Owner::Combat && event.type != Walker::EventType::Failed)
      continue;
    if (event.owner == Walker::Owner::Quest) {
      if (event.type == Walker::EventType::Completed)
        g_completedQuestStep = event.targetFormId;
      else if (event.type == Walker::EventType::Failed)
        ClearCurrentQuestRoute();
    }
    if (event.owner == Walker::Owner::Exploration && event.type == Walker::EventType::Completed) {
      g_recentExploration.push_back(event.targetFormId);
      if (g_recentExploration.size() > kRecentExplorationLimit)
        g_recentExploration.erase(g_recentExploration.begin());
    }

    std::string message;
    bool silent = true;
    switch (event.type) {
    case Walker::EventType::Failed:
      message = WalkerLogic::DescribeFailure(event.reason, event.description, event.detail);
      silent = false;
      break;
    case Walker::EventType::Progress:
      // A long walk would otherwise be silent after "Started": remind her where it stands.
      message = WalkerLogic::DescribeProgress(event.owner == Walker::Owner::Quest,
                                              event.owner == Walker::Owner::Exploration, event.description,
                                              event.walkedDistance, event.walkSeconds, event.remainingDistance);
      silent = false;
      break;
    case Walker::EventType::Paused:
      message = WalkerLogic::DescribePause(event.description, event.detail);
      break;
    case Walker::EventType::Started:
    case Walker::EventType::Completed:
      message = std::move(event.message);
      if (event.owner == Walker::Owner::Quest && g_currentQuestRoute &&
          IsDoorStep(event.targetFormId, *g_currentQuestRoute)) {
        const std::string door = DoorLabel(LookupReference(event.targetFormId));
        if (event.type == Walker::EventType::Started)
          message = "Heading to " + door + " to enter another area. The objective itself, " +
                    g_currentQuestRoute->description + ", is beyond it.";
        else
          message = "Reached and used " + door + ". That was only the door on the way: the objective " +
                    g_currentQuestRoute->description + " is NOT done yet, and the route continues on its own.";
      }
      break;
    }
    NeuroSDK::SendContext(message.c_str(), silent);
  }
}

// A "You are now ..." line when the place changed and stayed changed (see Location::PlaceTracker). Only runs while
// gameplay is unblocked, so a load screen or a door transition is over before the line is considered.
void ObservePlace() {
  const auto now = std::chrono::steady_clock::now();
  if (now < g_nextPlaceObservation)
    return;
  g_nextPlaceObservation = now + kPlaceObservationInterval;
  const float elapsed = g_lastPlaceObservation.time_since_epoch().count() == 0
                            ? 0.0f
                            : std::min(std::chrono::duration<float>(now - g_lastPlaceObservation).count(), 2.0f);
  g_lastPlaceObservation = now;
  const auto place = ReadPlace();
  if (!place)
    return;
  if (auto line = g_place.Update(*place, elapsed)) {
    _MESSAGE("Location: %s", line->c_str());
    NeuroSDK::SendContext(line->c_str(), false);
  }
}

void ContinueCurrentQuest() {
  if (!g_currentQuestRoute)
    return;
  auto *player = PlayerCharacter::GetSingleton();
  auto *quest = LookupQuest(g_currentQuestRoute->questFormId);
  if (!player || !quest || player->activeQuest != quest) {
    ClearCurrentQuestRoute();
    return;
  }
  auto *objective = FindObjective(g_currentQuestRoute->questFormId, g_currentQuestRoute->objectiveId);
  if (!objective || (quest->flags & kQuestFlagFinished) ||
      (objective->status & kObjectiveDisplayMask) != BGSQuestObjective::eQObjStatus_displayed) {
    ClearCurrentQuestRoute();
    return;
  }
  auto *target = ResolveQuestTarget(objective, g_currentQuestRoute->targetFormId);
  if (!target) {
    auto *time = TimeGlobal::Get();
    g_questResolveTime += time ? std::clamp(time->secondsPassed, 0.0f, 0.1f) : 0.0f;
    if (g_questResolveTime >= kQuestResolveTimeout) {
      NeuroSDK::SendContext(
          ("Could not resolve the next route step for " + g_currentQuestRoute->description + ".").c_str(), false);
      ClearCurrentQuestRoute();
    }
    return;
  }
  g_questResolveTime = 0.0f;
  if (target->GetFormID() == g_completedQuestStep)
    return;
  if (Walker::IsActive() && Walker::GetOwner() != Walker::Owner::Quest)
    return;
  if (!Walker::IsActive() || Walker::GetTargetFormId() != target->GetFormID()) {
    g_completedQuestStep = 0;
    const Walker::Intent intent =
        Walker::CanInteract(target->GetFormID()) ? Walker::Intent::Interact : Walker::Intent::Move;
    if (!Walker::Start(target->GetFormID(), intent, Walker::Owner::Quest,
                       StepDescription(target, *g_currentQuestRoute))) {
      NeuroSDK::SendContext(
          ("Could not start the next route step for " + g_currentQuestRoute->description + ".").c_str(), false);
      ClearCurrentQuestRoute();
    }
  }
}

} // namespace

void Process(bool gameplayBlocked) {
  g_gameplayBlocked = gameplayBlocked;
  if (!g_ready || !PlayerCharacter::GetSingleton()) {
    g_worldActions.Unregister();
    g_questActions.Unregister();
    g_moveActions.Unregister();
    return;
  }
  BuildActions();
  // Menus are meaningful availability boundaries, not a reason to expose unusable world actions.
  // Quest inspection/selection remains useful in menus; unchanged frames send no registration messages.
  if (gameplayBlocked)
    g_worldActions.Unregister();
  else
    g_worldActions.Register();
  g_questActions.Register();
  g_moveActions.Register();
  ProcessWalkerEvents();
  if (gameplayBlocked)
    return;
  ObservePlace();
  // Quest state is compared against the previous snapshot, so a sparse sample loses no transitions.
  const auto now = std::chrono::steady_clock::now();
  if (now >= g_nextQuestObservation) {
    ObserveQuests();
    g_nextQuestObservation = now + kQuestObservationInterval;
  }
  ContinueCurrentQuest();
}

void SetReady(bool ready) {
  g_ready = ready;
  if (!ready)
    Reset();
}

bool IsReady() { return g_ready && PlayerCharacter::GetSingleton(); }

bool ValidateQuestAction(std::string &error) {
  if (!g_ready || !PlayerCharacter::GetSingleton()) {
    error = "Quest actions are unavailable until a game is loaded.";
    return false;
  }
  return true;
}

bool ValidateGameplayAction(std::string &error) {
  if (!ValidateQuestAction(error))
    return false;
  if (g_gameplayBlocked || Menus::PipBoyHandler::IsExecuting()) {
    error = "Gameplay actions are unavailable while a menu is open. Finish or close the current menu first.";
    return false;
  }
  return true;
}

void Reset() {
  // Unbind immediately and retain failed unregisters for retry, including across reconnects.
  g_worldActions.Unregister();
  g_questActions.Unregister();
  g_moveActions.Unregister();
  g_gameplayBlocked = true;
  g_quests.clear();
  g_objects.clear();
  ClearCurrentQuestRoute();
  g_recentExploration.clear();
  g_selectedObjective.reset();
  g_objectiveObservations.clear();
  g_questFlags.clear();
  g_observationInitialized = false;
  g_nextQuestObservation = {};
  g_place.Reset();
  g_nextPlaceObservation = {};
  g_lastPlaceObservation = {};
}

std::string BuildGreeting() {
  if (!IsReady())
    return {};
  std::string location;
  if (const auto place = ReadPlace())
    location = "You are currently " + Location::DescribePlace(*place) + ".";
  return Context::BuildGreeting(location);
}

std::string BuildFreshStatus() {
  if (!g_ready || !PlayerCharacter::GetSingleton())
    return {};

  constexpr size_t kMaxObjectivesListed = 5;
  std::ostringstream status;
  status << "## Status after reconnecting\n"
         << "Your connection to the game was restored. Anything that happened while you were disconnected was not "
            "reported to you; this is where things stand now.";
  status << "\n\n" << BuildGreeting();
  status << "\n\n" << CombatHandler::BuildPlayerStatus();

  // Names only: this must not rebuild the query_quests catalog, whose ids Neuro may already be using.
  status << "\n\n## Active quest objectives";
  size_t listed = 0;
  size_t total = 0;
  auto *player = PlayerCharacter::GetSingleton();
  for (auto iter = player->questObjectiveList.Begin(); !iter.End(); ++iter) {
    auto *objective = iter.Get();
    if (!objective || !objective->quest ||
        (objective->status & kObjectiveDisplayMask) != BGSQuestObjective::eQObjStatus_displayed ||
        (objective->quest->flags & kQuestFlagFinished))
      continue;
    ++total;
    if (listed < kMaxObjectivesListed) {
      status << "\n- " << QuestName(objective->quest) << ": " << ObjectiveText(objective);
      ++listed;
    }
  }
  if (total == 0)
    status << "\n- None.";
  else if (total > listed)
    status << "\n- and " << (total - listed) << " more (use query_quests)";
  return status.str();
}

void QueryQuests() {
  BuildQuestCatalog();
  std::ostringstream context;
  context << "## Current quest objectives";
  if (g_quests.empty()) {
    context << "\n- None.";
  } else {
    for (const auto &entry : g_quests) {
      context << "\n- `" << entry.id << "` - " << entry.description;
      context << " - " << DescribeQuestLocation(entry);
    }
  }
  NeuroSDK::SendContext(context.str().c_str(), true);
}

bool ValidateQuestSelection(int id, QuestSelection &selection, std::string &error) {
  if (!ValidateQuestAction(error))
    return false;
  auto entry = std::find_if(g_quests.begin(), g_quests.end(), [id](const QuestEntry &quest) { return quest.id == id; });
  if (entry == g_quests.end()) {
    error = ValidQuestIds();
    return false;
  }
  selection = *entry;
  return RevalidateQuestSelection(selection, error);
}

bool RevalidateQuestSelection(const QuestSelection &selection, std::string &error) {
  if (!ValidateQuestAction(error))
    return false;
  auto *objective = FindObjective(selection.questFormId, selection.objectiveId);
  auto *target = LookupReference(selection.targetFormId);
  auto *quest = LookupQuest(selection.questFormId);
  if (!quest || (quest->flags & kQuestFlagFinished) || !objective || !target ||
      !FindObjectiveTarget(objective, selection.targetFormId) ||
      (objective->status & kObjectiveDisplayMask) != BGSQuestObjective::eQObjStatus_displayed) {
    error = "That quest target is no longer active. Run query_quests again.";
    return false;
  }
  return true;
}

void SelectQuest(const QuestSelection &selection) {
  auto *quest = LookupQuest(selection.questFormId);
  if (!quest)
    return;
  if (Walker::IsActive() && Walker::GetOwner() == Walker::Owner::Quest)
    Walker::Stop();
  ClearCurrentQuestRoute();
  g_selectedObjective = selection;
  ActivateQuest(quest);
  const std::string context = "## Selected quest\n" + selection.description;
  NeuroSDK::SendContext(context.c_str(), true);
}

bool PrepareCurrentQuest(QuestSelection &selection, std::string &error) {
  if (!ValidateGameplayAction(error))
    return false;
  auto *player = PlayerCharacter::GetSingleton();
  auto *quest = player ? player->activeQuest : nullptr;
  if (!quest) {
    error = "No quest is currently selected. Use query_quests and select_quest first.";
    return false;
  }
  if (quest->flags & kQuestFlagFinished) {
    error = "The currently selected quest is already completed or failed.";
    return false;
  }

  // A quest can have several displayed objectives. The objective Neuro picked with select_quest is tried first (and
  // inside it the target she picked), then the others in the engine's order; the first one with a way to it wins.
  const bool hasChosen = g_selectedObjective && g_selectedObjective->questFormId == quest->GetFormID();
  std::vector<BGSQuestObjective *> objectives;
  for (auto objectiveIter = player->questObjectiveList.Begin(); !objectiveIter.End(); ++objectiveIter) {
    auto *objective = objectiveIter.Get();
    if (objective && objective->quest == quest &&
        (objective->status & kObjectiveDisplayMask) == BGSQuestObjective::eQObjStatus_displayed)
      objectives.push_back(objective);
  }
  if (hasChosen) {
    std::stable_partition(objectives.begin(), objectives.end(), [&](BGSQuestObjective *objective) {
      return objective->objectiveId == g_selectedObjective->objectiveId;
    });
  }
  for (auto *objective : objectives) {
    auto steps = CollectQuestSteps(objective, 0);
    if (hasChosen && objective->objectiveId == g_selectedObjective->objectiveId) {
      std::stable_partition(steps.begin(), steps.end(), [&](const QuestStep &step) {
        return step.target->target->GetFormID() == g_selectedObjective->targetFormId;
      });
    }
    if (steps.empty())
      continue;
    selection = {quest->GetFormID(), objective->objectiveId, steps.front().target->target->GetFormID(),
                 QuestName(quest) + ": " + ObjectiveText(objective)};
    return true;
  }

  error = objectives.empty() ? "The currently selected quest has no active objective."
                             : "The current quest objective has no reachable target.";
  return false;
}

bool RevalidateCurrentQuest(const QuestSelection &selection, std::string &error) {
  if (!ValidateGameplayAction(error))
    return false;
  auto *player = PlayerCharacter::GetSingleton();
  auto *quest = LookupQuest(selection.questFormId);
  if (!player || !quest || player->activeQuest != quest) {
    error = "The currently selected quest changed before execution.";
    return false;
  }
  if (!RevalidateQuestSelection(selection, error)) {
    error = "The current quest objective is no longer active.";
    return false;
  }
  auto *objective = FindObjective(selection.questFormId, selection.objectiveId);
  if (!ResolveQuestTarget(objective, selection.targetFormId)) {
    error = "The current quest objective no longer has a reachable route.";
    return false;
  }
  return true;
}

void DoCurrentQuest(const QuestSelection &selection) {
  ClearCurrentQuestRoute();
  g_currentQuestRoute = selection;
  auto *objective = FindObjective(selection.questFormId, selection.objectiveId);
  auto *target = ResolveQuestTarget(objective, selection.targetFormId);
  const Walker::Intent intent =
      target && Walker::CanInteract(target->GetFormID()) ? Walker::Intent::Interact : Walker::Intent::Move;
  if (!target ||
      !Walker::Start(target->GetFormID(), intent, Walker::Owner::Quest, StepDescription(target, selection))) {
    NeuroSDK::SendContext(("Could not begin following " + selection.description + ".").c_str(), false);
    ClearCurrentQuestRoute();
  }
}

void QueryNearby() {
  auto scanned = ScanObjects(kNearbyRadius, CollectQuestTargetIds());
  std::vector<Objects::Candidate> candidates;
  candidates.reserve(scanned.size());
  for (const auto &entry : scanned)
    candidates.push_back({entry.referenceFormId, entry.category, entry.distance, entry.questTarget});
  const size_t total = Objects::Rank(candidates, kNearbyLimit);

  // Ids follow the ranking, so the same surroundings always give the same ids.
  g_objects.clear();
  for (const auto &candidate : candidates) {
    auto found = std::find_if(scanned.begin(), scanned.end(),
                              [&](const ObjectEntry &entry) { return entry.referenceFormId == candidate.formId; });
    g_objects.push_back(std::move(*found));
    g_objects.back().id = static_cast<int>(g_objects.size());
  }

  std::ostringstream context;
  context << "## Nearby objects";
  if (g_objects.empty()) {
    context << "\n- None found in the loaded area.";
  } else {
    for (const auto &entry : g_objects) {
      context << "\n- `" << entry.id << "` - **" << Objects::CategoryName(entry.category) << "** - " << entry.name
              << " - " << Units::FormatDistance(entry.distance) << (entry.questTarget ? " - quest target" : "");
    }
    const std::string note = Objects::TruncationNote(g_objects.size(), total);
    if (!note.empty())
      context << "\n\n" << note;
  }
  NeuroSDK::SendContext(context.str().c_str(), true);
}

bool ValidateObjectSelection(int id, ObjectSelection &selection, std::string &error) {
  if (!ValidateGameplayAction(error))
    return false;
  auto entry =
      std::find_if(g_objects.begin(), g_objects.end(), [id](const ObjectEntry &object) { return object.id == id; });
  if (entry == g_objects.end()) {
    error = ValidObjectIds();
    return false;
  }
  selection = *entry;
  return RevalidateObjectSelection(selection, error);
}

bool RevalidateObjectSelection(const ObjectSelection &selection, std::string &error) {
  if (!ValidateGameplayAction(error))
    return false;
  auto *player = PlayerCharacter::GetSingleton();
  auto *ref = LookupReference(selection.referenceFormId);
  if (!player || !ref || !ref->GetParentCell() || ref->GetParentCell()->GetFormID() != selection.cellFormId ||
      !IsInSameTravelSpace(player, ref) || Distance3D(player->GetPos(), ref->GetPos()) > kExploreRadius) {
    error = selection.name + " is no longer available in the current area.";
    return false;
  }
  return true;
}

namespace {
// Walker refused a command the action had already accepted; Neuro hears why it never started.
void ReportStartFailure(const std::string &what) {
  NeuroSDK::SendContext(("Could not start " + what + ".").c_str(), false);
}
} // namespace

void StartObjectAction(const ObjectSelection &selection, Walker::Intent intent) {
  if (!Walker::Start(selection.referenceFormId, intent, Walker::Owner::Object, selection.name))
    ReportStartFailure("moving to " + selection.name);
}

bool PrepareExploration(ObjectSelection &selection, std::string &error) {
  if (!ValidateGameplayAction(error))
    return false;
  auto candidates = ScanObjects(kExploreRadius);
  std::erase_if(candidates, [](const ObjectEntry &entry) {
    return entry.distance < kExploreMinimumDistance || std::find(g_recentExploration.begin(), g_recentExploration.end(),
                                                                 entry.referenceFormId) != g_recentExploration.end();
  });
  if (candidates.empty()) {
    error = "No unexplored interesting objects are available in the loaded area.";
    return false;
  }

  const size_t variedCandidateCount = std::min<size_t>(candidates.size(), 12);
  g_randomState = g_randomState * 1664525u + 1013904223u;
  const auto &candidate = candidates[g_randomState % variedCandidateCount];
  selection = candidate;
  return RevalidateObjectSelection(selection, error);
}

void StartExploration(const ObjectSelection &selection) {
  if (!Walker::Start(selection.referenceFormId, Walker::Intent::Move, Walker::Owner::Exploration, selection.name))
    ReportStartFailure("exploring toward " + selection.name);
}

bool PrepareStopMove(MoveStop &stop, std::string &error) {
  if (!ValidateQuestAction(error))
    return false;
  if (Walker::IsActive() && Walker::GetOwner() == Walker::Owner::Combat) {
    // The engagement would send her straight back; walking and attacking are separate verbs.
    error = "You are closing in on a target to fight it. Use stop_attack to stop fighting instead.";
    return false;
  }
  if (!Walker::IsActive() && !g_currentQuestRoute) {
    error = "You are not moving toward anything right now.";
    return false;
  }
  stop.owner = Walker::IsActive() ? Walker::GetOwner() : Walker::Owner::Quest;
  stop.targetFormId = Walker::IsActive() ? Walker::GetTargetFormId() : 0;
  stop.description = Walker::IsActive() ? Walker::GetDescription() : g_currentQuestRoute->description;
  return true;
}

bool RevalidateStopMove(const MoveStop &stop, std::string &error) {
  MoveStop current;
  if (!PrepareStopMove(current, error))
    return false;
  if (current.owner != stop.owner || current.targetFormId != stop.targetFormId) {
    error = "Your movement changed before it could be stopped. Call stop_move again if you still want to stop.";
    return false;
  }
  return true;
}

void StopMove(const MoveStop &stop) {
  // The route must go first: ContinueCurrentQuest() restarts Walker for the next step every frame otherwise.
  ClearCurrentQuestRoute();
  Walker::Stop();
  _MESSAGE("Movement stopped on request (owner %d, target %08X)", static_cast<int>(stop.owner), stop.targetFormId);
  NeuroSDK::SendContext(("## Movement\nYou stopped moving" +
                         (stop.description.empty() ? std::string() : " toward " + stop.description) + ".")
                            .c_str(),
                        true);
}

} // namespace GameplayHandler
