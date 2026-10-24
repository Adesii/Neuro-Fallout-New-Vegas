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
#include "GameData.h"
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
#include <utility>
#include <vector>

namespace GameplayHandler {
namespace {

using WorldUtils::Distance3D;
using WorldUtils::LookupReference;

constexpr float kNearbyRadius = 2500.0f;
constexpr float kExploreRadius = 5000.0f;
constexpr float kExploreMinimumDistance = 300.0f;
constexpr size_t kNearbyLimit = 30;
constexpr size_t kRecentExplorationLimit = 8;
constexpr float kQuestResolveTimeout = 10.0f;
constexpr auto kQuestObservationInterval = std::chrono::milliseconds(500);

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
  std::string category;
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

TESObjectREFR *ResolveQuestTarget(BGSQuestObjective *objective, uint32_t preferredTargetId) {
  auto *player = PlayerCharacter::GetSingleton();
  if (!player || !objective)
    return nullptr;

  for (auto iter = objective->targets.Begin(); !iter.End(); ++iter) {
    auto *target = iter.Get();
    if (!target || !target->target || target->target->GetDeleted() || target->target->GetDisabled())
      continue;
    bool conditionResult = false;
    if (!target->conditions.Empty() && !target->conditions.Evaluate(target->target, nullptr, &conditionResult, false))
      continue;
    if (preferredTargetId && target->target->GetFormID() != preferredTargetId)
      continue;

    if (IsInSameTravelSpace(player, target->target))
      return target->target;
    return FindEntryDoor(target);
  }
  return nullptr;
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

std::string CategoryFor(TESObjectREFR *ref) {
  if (!ref || !ref->baseForm)
    return {};
  if (ref->IsActor()) {
    auto *actor = static_cast<Actor *>(ref);
    return actor->GetDead() ? "corpse" : "actor";
  }
  switch (ref->baseForm->eFormType) {
  case _FormType::TESObjectACTI:
  case _FormType::BGSTalkingActivator:
    return "activator";
  case _FormType::TESObjectCONT:
    return "container";
  case _FormType::TESObjectDOOR:
    return "door";
  case _FormType::BGSTerminal:
    return "terminal";
  case _FormType::TESFlora:
    return "harvestable";
  case _FormType::TESFurniture:
    return "furniture";
  case _FormType::TESObjectWEAP:
    return "weapon";
  case _FormType::TESObjectARMO:
  case _FormType::TESObjectCLOT:
    return "armor";
  case _FormType::TESAmmo:
    return "ammo";
  case _FormType::AlchemyItem:
  case _FormType::IngredientItem:
    return "aid";
  case _FormType::TESObjectBOOK:
  case _FormType::BGSNote:
    return "readable";
  case _FormType::TESKey:
  case _FormType::TESObjectMISC:
    return "loot";
  default:
    return {};
  }
}

std::vector<ObjectEntry> ScanObjects(float radius) {
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
    std::string category = CategoryFor(ref);
    if (category.empty())
      return;
    const float distance = Distance3D(player->GetPos(), ref->GetPos());
    if (!std::isfinite(distance) || distance > radius)
      return;
    objects.push_back({{ref->GetFormID(), cell->GetFormID(), name}, 0, distance, std::move(category)});
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
      if (!target || !target->target || target->target->GetDeleted() || target->target->GetDisabled())
        continue;
      bool conditionResult = false;
      if (!target->conditions.Empty() && !target->conditions.Evaluate(target->target, nullptr, &conditionResult, false))
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
    for (const auto &[questId, flags] : currentQuestFlags) {
      const uint8_t previous = g_questFlags.contains(questId) ? g_questFlags[questId] : flags;
      auto *quest = LookupQuest(questId);
      if (!(previous & kQuestFlagCompleted) && (flags & kQuestFlagCompleted))
        notifications.push_back("Quest completed: " + QuestName(quest));
      if (!(previous & kQuestFlagFailed) && (flags & kQuestFlagFailed))
        notifications.push_back("Quest failed: " + QuestName(quest));
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
      else if (event.type == Walker::EventType::Failed) {
        ClearCurrentQuestRoute();
      }
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
}

std::string BuildFreshStatus() {
  if (!g_ready || !PlayerCharacter::GetSingleton())
    return {};

  constexpr size_t kMaxObjectivesListed = 5;
  std::ostringstream status;
  status << "## Status after reconnecting\n"
         << "Your connection to the game was restored. Anything that happened while you were disconnected was not "
            "reported to you; this is where things stand now.";
  // Slots for the greeting (todo 24) and the current location (todo 28) go here once those tasks exist.
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

  // A quest can have several displayed objectives; the one Neuro picked with select_quest comes first.
  const bool hasChosen = g_selectedObjective && g_selectedObjective->questFormId == quest->GetFormID();
  bool hasActiveObjective = false;
  for (int pass = hasChosen ? 0 : 1; pass < 2; ++pass) {
    for (auto objectiveIter = player->questObjectiveList.Begin(); !objectiveIter.End(); ++objectiveIter) {
      auto *objective = objectiveIter.Get();
      if (!objective || objective->quest != quest ||
          (objective->status & kObjectiveDisplayMask) != BGSQuestObjective::eQObjStatus_displayed)
        continue;
      if (pass == 0 && objective->objectiveId != g_selectedObjective->objectiveId)
        continue;
      hasActiveObjective = true;
      for (auto targetIter = objective->targets.Begin(); !targetIter.End(); ++targetIter) {
        auto *target = targetIter.Get();
        if (!target || !target->target || !ResolveQuestTarget(objective, target->target->GetFormID()))
          continue;
        if (pass == 0 && target->target->GetFormID() != g_selectedObjective->targetFormId)
          continue;
        selection = {quest->GetFormID(), objective->objectiveId, target->target->GetFormID(),
                     QuestName(quest) + ": " + ObjectiveText(objective)};
        return true;
      }
    }
  }

  error = hasActiveObjective ? "The current quest objective has no reachable target."
                             : "The currently selected quest has no active objective.";
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
  g_objects = ScanObjects(kNearbyRadius);
  if (g_objects.size() > kNearbyLimit)
    g_objects.resize(kNearbyLimit);
  for (size_t index = 0; index < g_objects.size(); ++index)
    g_objects[index].id = static_cast<int>(index + 1);

  std::ostringstream context;
  context << "## Nearby objects";
  if (g_objects.empty()) {
    context << "\n- None found in the loaded area.";
  } else {
    for (const auto &entry : g_objects) {
      context << "\n- `" << entry.id << "` - **" << entry.category << "** - " << entry.name << " - "
              << Units::FormatDistance(entry.distance);
    }
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

void StartObjectAction(const ObjectSelection &selection, Walker::Intent intent) {
  if (!Walker::Start(selection.referenceFormId, intent, Walker::Owner::Object, selection.name))
    NeuroSDK::SendContext(("Could not start moving to " + selection.name + ".").c_str(), false);
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
    NeuroSDK::SendContext(("Could not begin exploring toward " + selection.name + ".").c_str(), false);
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
