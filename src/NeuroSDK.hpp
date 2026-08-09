#pragma once
#include "Actions/Action.hpp"
#include "common/ISingleton.h"
#include "neurosdk.h"
#include "nvse/CommandTable.h"
#include "nvse/ParamInfos.h"
#include "nvse/PluginAPI.h"

#include "common.hpp"
#include <string>
#include <vector>
class NeuroSDK : public ISingleton<NeuroSDK> {
public:
  enum class ActionPriority { Low, Medium, High, Critical };

  bool Initialize();
  void MainLoop();
  void ResetAutomation();
  static bool SendContext(const char *message, bool silent = false);
  static bool RegisterActions(const std::vector<Actions::Definition> &definitions);
  static bool UnregisterActions(const std::vector<std::string> &names);
  static bool ForceActions(const std::vector<std::string> &names, const std::string &query, const std::string &state,
                           ActionPriority priority, bool ephemeralContext = false);
  static bool SendActionResult(const std::string &id, bool success, const std::string &message);
  void RegisterCommands(NVSEInterface *nvse);

  static std::string GetCharacterDisplayName();

private:
  neurosdk_context_t ctx{};
  bool isConnected = false;
  std::vector<Actions::Request> actionInbox;

  void StartupMessage();
  bool SendSDKMessage(neurosdk_message_t &message);
  bool PollMessages();
  std::vector<Actions::Request> TakeActionInbox();
};
