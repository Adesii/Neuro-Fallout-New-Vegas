#pragma once

#include "WalkerHandler.hpp"
#include <cstdint>
#include <string>

namespace GameplayHandler {

struct QuestSelection {
  uint32_t questFormId = 0;
  uint32_t objectiveId = 0;
  uint32_t targetFormId = 0;
  std::string description;
};

struct ObjectSelection {
  uint32_t referenceFormId = 0;
  uint32_t cellFormId = 0;
  std::string name;
};

void Process(bool gameplayBlocked);
void Reset();
void SetReady(bool ready);

void QueryQuests();
bool ValidateQuestSelection(int id, QuestSelection &selection, std::string &error);
bool RevalidateQuestSelection(const QuestSelection &selection, std::string &error);
void SelectQuest(const QuestSelection &selection);

void QueryNearby();
bool ValidateObjectSelection(int id, ObjectSelection &selection, std::string &error);
bool RevalidateObjectSelection(const ObjectSelection &selection, std::string &error);
void StartObjectAction(const ObjectSelection &selection, Walker::Intent intent);

bool PrepareExploration(ObjectSelection &selection, std::string &error);
void StartExploration(const ObjectSelection &selection);

} // namespace GameplayHandler
