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
// A game is loaded and the player exists (the probe other handlers used to get from ValidateQuestAction).
bool IsReady();
bool ValidateQuestAction(std::string &error);
bool ValidateGameplayAction(std::string &error);

// The orientation message sent after a reconnect; empty until a game is loaded. It leads with the greeting.
std::string BuildFreshStatus();
// The silent greeting (connect, load, new game, every ten minutes); empty until a game is loaded.
std::string BuildGreeting();

void QueryQuests();
bool ValidateQuestSelection(int id, QuestSelection &selection, std::string &error);
bool RevalidateQuestSelection(const QuestSelection &selection, std::string &error);
void SelectQuest(const QuestSelection &selection);
bool PrepareCurrentQuest(QuestSelection &selection, std::string &error);
bool RevalidateCurrentQuest(const QuestSelection &selection, std::string &error);
void DoCurrentQuest(const QuestSelection &selection);
// Neuro started `command` herself. When that ends the do_current_quest route (WalkerLogic::EndsQuestRoute, todo 50)
// and one is running, the route is cleared, a walk it owns is stopped, and she gets one silent note saying so. The
// route stays off until do_current_quest is called again. Call it once the command is accepted, before it starts
// its own walk. True when a route was ended.
bool InterruptQuestRoute(WalkerLogic::Command command);

// `page` is one-based; an empty `search` lists everything (Nearby::ParseQuery has validated both).
void QueryNearby(int page, const std::string &search);
bool ValidateObjectSelection(int id, ObjectSelection &selection, std::string &error);
bool RevalidateObjectSelection(const ObjectSelection &selection, std::string &error);
void StartObjectAction(const ObjectSelection &selection, Walker::Intent intent);

bool PrepareExploration(ObjectSelection &selection, std::string &error);
void StartExploration(const ObjectSelection &selection);

// What stop_move is about to cancel: the Walker command (owner and target) or only a quest route between steps.
struct MoveStop {
  Walker::Owner owner = Walker::Owner::None;
  uint32_t targetFormId = 0;
  std::string description;
};
bool PrepareStopMove(MoveStop &stop, std::string &error);
bool RevalidateStopMove(const MoveStop &stop, std::string &error);
void StopMove(const MoveStop &stop);

} // namespace GameplayHandler
