#include "GameplayHandler.hpp"
#include "Actions/Gameplay/ExploreAction.hpp"
#include "Actions/Gameplay/QueryNearbyAction.hpp"
#include "Actions/Gameplay/QueryQuestsAction.hpp"
#include "Actions/Gameplay/SelectQuestAction.hpp"
#include "Actions/Gameplay/TargetObjectAction.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "GameData.h"
#include "GameObjects.h"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "decoding.h"
#include "nvse/GameForms.h"
#include "utils/math.h"
#include <algorithm>
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

constexpr float kNearbyRadius = 2500.0f;
constexpr float kExploreRadius = 5000.0f;
constexpr float kExploreMinimumDistance = 300.0f;
constexpr size_t kNearbyLimit = 30;
constexpr size_t kRecentExplorationLimit = 8;
constexpr float kQuestResolveTimeout = 10.0f;

struct QuestEntry : QuestSelection {
  int id = 0;
  float distance = 0.0f;
  bool sameTravelSpace = false;
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

Actions::PersistentActionSet g_actions;
bool g_actionsBuilt = false;
bool g_ready = false;
std::vector<QuestEntry> g_quests;
std::vector<ObjectEntry> g_objects;
std::optional<QuestSelection> g_selectedQuest;
uint32_t g_completedQuestStep = 0;
float g_questResolveTime = 0.0f;
std::vector<uint32_t> g_recentExploration;
uint32_t g_randomState = 0x6D2B79F5;
bool g_observationInitialized = false;
std::unordered_map<uint64_t, ObjectiveObservation> g_objectiveObservations;
std::unordered_map<uint32_t, uint8_t> g_questFlags;

void BuildActions() {
  if (g_actionsBuilt)
    return;
  g_actions.Add(std::make_unique<Actions::Gameplay::QueryQuestsAction>())
      .Add(std::make_unique<Actions::Gameplay::SelectQuestAction>())
      .Add(std::make_unique<Actions::Gameplay::QueryNearbyAction>())
      .Add(std::make_unique<Actions::Gameplay::TargetObjectAction>(false))
      .Add(std::make_unique<Actions::Gameplay::TargetObjectAction>(true))
      .Add(std::make_unique<Actions::Gameplay::ExploreAction>());
  g_actionsBuilt = true;
}

TESObjectREFR *LookupReference(uint32_t formId) {
  auto *form = TESForm::GetFormByNumericID(formId);
  if (!form || !form->IsReference())
    return nullptr;
  auto *ref = static_cast<TESObjectREFR *>(form);
  return ref->GetDeleted() || ref->GetDisabled() ? nullptr : ref;
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
    if (!target->data.teleportLinks.IsEmpty() && target->data.teleportLinks.pBuffer) {
      auto *door = target->data.teleportLinks.GetAt(0).door;
      if (door && !door->GetDeleted())
        return door;
    }
    return nullptr;
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

float Distance3D(const NiPoint3 &left, const NiPoint3 &right) {
  const float x = left.x - right.x;
  const float y = left.y - right.y;
  const float z = left.z - right.z;
  return std::sqrt(x * x + y * y + z * z);
}

std::vector<ObjectEntry> ScanObjects(float radius) {
  std::vector<ObjectEntry> objects;
  auto *player = PlayerCharacter::GetSingleton();
  if (!player)
    return objects;
  auto *tes = TES::GetSingleton();
  if (!tes)
    return objects;

  std::unordered_set<uint32_t> seen;
  auto scanCell = [&](TESObjectCELL *cell) {
    if (!cell)
      return;
    for (auto *node = cell->objectList.GetHead(); node; node = node->GetNext()) {
      auto *ref = node->GetItem();
      if (!ref || ref == player || ref->GetDeleted() || ref->GetDisabled() || !ref->baseForm ||
          !seen.insert(ref->GetFormID()).second)
        continue;
      const char *name = ref->GetFullName();
      if (!name || !name[0])
        continue;
      std::string category = CategoryFor(ref);
      if (category.empty())
        continue;
      const float distance = Distance3D(player->GetPos(), ref->GetPos());
      if (!std::isfinite(distance) || distance > radius)
        continue;
      objects.push_back({{ref->GetFormID(), cell->GetFormID(), name}, 0, distance, std::move(category)});
    }
  };

  if (tes->currentInterior) {
    scanCell(tes->currentInterior);
  } else if (tes->gridCellArray && tes->gridCellArray->pGridCells) {
    const int dimension = tes->gridCellArray->iDimension;
    for (int y = 0; y < dimension; ++y) {
      for (int x = 0; x < dimension; ++x) {
        auto *gridCell = tes->gridCellArray->GetCell(x, y);
        if (gridCell)
          scanCell(gridCell->pCell);
      }
    }
  } else {
    scanCell(player->GetParentCell());
  }

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
    if (!objective || !objective->quest || (objective->status & 3) != BGSQuestObjective::eQObjStatus_displayed ||
        (objective->quest->flags & (2 | 0x40)))
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
      g_quests.push_back(
          {{objective->quest->GetFormID(), objective->objectiveId, target->target->GetFormID(), description},
           0,
           distance,
           sameSpace});
    }
  }

  std::sort(g_quests.begin(), g_quests.end(), [](const QuestEntry &left, const QuestEntry &right) {
    auto *leftQuest = LookupQuest(left.questFormId);
    auto *rightQuest = LookupQuest(right.questFormId);
    const int leftPriority = leftQuest ? leftQuest->priority : 0;
    const int rightPriority = rightQuest ? rightQuest->priority : 0;
    if (leftPriority != rightPriority)
      return leftPriority > rightPriority;
    if (left.questFormId != right.questFormId)
      return left.questFormId < right.questFormId;
    if (left.objectiveId != right.objectiveId)
      return left.objectiveId < right.objectiveId;
    return left.targetFormId < right.targetFormId;
  });
  for (size_t index = 0; index < g_quests.size(); ++index)
    g_quests[index].id = static_cast<int>(index + 1);
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
      if ((observation.status & 3) == BGSQuestObjective::eQObjStatus_displayed)
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
      if (!(previous & 2) && (flags & 2))
        notifications.push_back("Quest completed: " + QuestName(quest));
      if (!(previous & 0x40) && (flags & 0x40))
        notifications.push_back("Quest failed: " + QuestName(quest));
    }
  }

  g_objectiveObservations = std::move(currentObjectives);
  g_questFlags = std::move(currentQuestFlags);
  g_observationInitialized = true;
  if (!notifications.empty()) {
    std::ostringstream context;
    for (size_t index = 0; index < notifications.size(); ++index) {
      if (index)
        context << '\n';
      context << notifications[index];
    }
    NeuroSDK::SendContext(context.str().c_str(), false);
  }
}

void ProcessWalkerEvents() {
  for (auto &event : Walker::TakeEvents()) {
    if (event.owner == Walker::Owner::Quest) {
      if (event.type == Walker::EventType::Completed)
        g_completedQuestStep = event.targetFormId;
      else {
        g_selectedQuest.reset();
        g_completedQuestStep = 0;
        g_questResolveTime = 0.0f;
      }
    }
    if (event.owner == Walker::Owner::Exploration && event.type == Walker::EventType::Completed) {
      g_recentExploration.push_back(event.targetFormId);
      if (g_recentExploration.size() > kRecentExplorationLimit)
        g_recentExploration.erase(g_recentExploration.begin());
    }
    NeuroSDK::SendContext(event.message.c_str(), event.type == Walker::EventType::Completed);
  }
}

void ContinueSelectedQuest() {
  if (!g_selectedQuest)
    return;
  auto *player = PlayerCharacter::GetSingleton();
  auto *quest = LookupQuest(g_selectedQuest->questFormId);
  if (!player || !quest || player->activeQuest != quest) {
    g_selectedQuest.reset();
    g_completedQuestStep = 0;
    g_questResolveTime = 0.0f;
    return;
  }
  auto *objective = FindObjective(g_selectedQuest->questFormId, g_selectedQuest->objectiveId);
  if (!objective || (quest->flags & (2 | 0x40)) ||
      (objective->status & 3) != BGSQuestObjective::eQObjStatus_displayed) {
    g_selectedQuest.reset();
    g_completedQuestStep = 0;
    g_questResolveTime = 0.0f;
    return;
  }
  auto *target = ResolveQuestTarget(objective, g_selectedQuest->targetFormId);
  if (!target) {
    auto *time = TimeGlobal::Get();
    g_questResolveTime += time ? std::clamp(time->secondsPassed, 0.0f, 0.1f) : 0.0f;
    if (g_questResolveTime >= kQuestResolveTimeout) {
      NeuroSDK::SendContext(("Could not resolve the next route step for " + g_selectedQuest->description + ".").c_str(),
                            false);
      g_selectedQuest.reset();
      g_completedQuestStep = 0;
      g_questResolveTime = 0.0f;
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
    if (!Walker::Start(target->GetFormID(), intent, Walker::Owner::Quest, g_selectedQuest->description)) {
      NeuroSDK::SendContext(("Could not start the next route step for " + g_selectedQuest->description + ".").c_str(),
                            false);
      g_selectedQuest.reset();
      g_completedQuestStep = 0;
    }
  }
}

} // namespace

void Process(bool gameplayBlocked) {
  if (!g_ready)
    return;
  BuildActions();
  ProcessWalkerEvents();
  if (gameplayBlocked) {
    g_actions.Unregister();
    return;
  }
  if (!g_actions.IsRegistered() && PlayerCharacter::GetSingleton())
    g_actions.Register();
  ObserveQuests();
  ContinueSelectedQuest();
}

void SetReady(bool ready) {
  g_ready = ready;
  if (!ready)
    Reset();
}

void Reset() {
  g_actions.Abandon();
  g_quests.clear();
  g_objects.clear();
  g_selectedQuest.reset();
  g_completedQuestStep = 0;
  g_questResolveTime = 0.0f;
  g_recentExploration.clear();
  g_objectiveObservations.clear();
  g_questFlags.clear();
  g_observationInitialized = false;
}

void QueryQuests() {
  BuildQuestCatalog();
  std::ostringstream context;
  context << "Current selectable quest targets:";
  if (g_quests.empty()) {
    context << " none.";
  } else {
    for (const auto &entry : g_quests) {
      context << "\n[id " << entry.id << "] " << entry.description;
      if (entry.sameTravelSpace)
        context << " (about " << static_cast<int>(entry.distance) << " game units away)";
      else
        context << " (requires travel through another area)";
    }
  }
  NeuroSDK::SendContext(context.str().c_str(), true);
}

bool ValidateQuestSelection(int id, QuestSelection &selection, std::string &error) {
  auto entry = std::find_if(g_quests.begin(), g_quests.end(), [id](const QuestEntry &quest) { return quest.id == id; });
  if (entry == g_quests.end()) {
    error = ValidQuestIds();
    return false;
  }
  selection = *entry;
  return RevalidateQuestSelection(selection, error);
}

bool RevalidateQuestSelection(const QuestSelection &selection, std::string &error) {
  auto *objective = FindObjective(selection.questFormId, selection.objectiveId);
  auto *target = LookupReference(selection.targetFormId);
  auto *quest = LookupQuest(selection.questFormId);
  if (!quest || (quest->flags & (2 | 0x40)) || !objective || !target ||
      !FindObjectiveTarget(objective, selection.targetFormId) ||
      (objective->status & 3) != BGSQuestObjective::eQObjStatus_displayed) {
    error = "That quest target is no longer active. Run query_quests again.";
    return false;
  }
  return true;
}

void SelectQuest(const QuestSelection &selection) {
  auto *quest = LookupQuest(selection.questFormId);
  if (!quest)
    return;
  ActivateQuest(quest);
  g_selectedQuest = selection;
  g_completedQuestStep = 0;
  g_questResolveTime = 0.0f;
  auto *objective = FindObjective(selection.questFormId, selection.objectiveId);
  auto *target = ResolveQuestTarget(objective, selection.targetFormId);
  const Walker::Intent intent =
      target && Walker::CanInteract(target->GetFormID()) ? Walker::Intent::Interact : Walker::Intent::Move;
  if (!target || !Walker::Start(target->GetFormID(), intent, Walker::Owner::Quest, selection.description)) {
    NeuroSDK::SendContext(("Could not begin following " + selection.description + ".").c_str(), false);
    g_selectedQuest.reset();
    g_completedQuestStep = 0;
  }
}

void QueryNearby() {
  g_objects = ScanObjects(kNearbyRadius);
  if (g_objects.size() > kNearbyLimit)
    g_objects.resize(kNearbyLimit);
  for (size_t index = 0; index < g_objects.size(); ++index)
    g_objects[index].id = static_cast<int>(index + 1);

  std::ostringstream context;
  context << "Nearby interactable objects and loot:";
  if (g_objects.empty()) {
    context << " none found in the loaded area.";
  } else {
    for (const auto &entry : g_objects) {
      const char *range = entry.distance < 450.0f ? "very close" : entry.distance < 1500.0f ? "nearby" : "farther away";
      context << "\n[id " << entry.id << "] [" << entry.category << "] " << entry.name << " (" << range << ", "
              << static_cast<int>(entry.distance) << " game units)";
    }
  }
  NeuroSDK::SendContext(context.str().c_str(), true);
}

bool ValidateObjectSelection(int id, ObjectSelection &selection, std::string &error) {
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

} // namespace GameplayHandler
