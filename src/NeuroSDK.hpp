#pragma once
#include "Actions/Action.hpp"
#include "common/ISingleton.h"
#include "neurosdk.h"
#include "nvse/CommandTable.h"
#include "nvse/ParamInfos.h"
#include "nvse/PluginAPI.h"

#include "common.hpp"
#include <chrono>
#include <future>
#include <string>
#include <vector>
class NeuroSDK : public ISingleton<NeuroSDK> {
public:
  enum class ActionPriority { Low, Medium, High, Critical };

  bool Initialize();
  void MainLoop();
  // connectionLost additionally drops results still owed for request ids of the dead session.
  void ResetAutomation(bool connectionLost = false);
  static bool SendContext(const char *message, bool silent = false);
  static bool RegisterActions(const std::vector<Actions::Definition> &definitions);
  static bool UnregisterActions(const std::vector<std::string> &names);
  static bool ForceActions(const std::vector<std::string> &names, const std::string &query, const std::string &state,
                           ActionPriority priority, bool ephemeralContext = false);
  static bool SendActionResult(const std::string &id, bool success, const std::string &message);
  void RegisterCommands(NVSEInterface *nvse);

  static std::string GetCharacterDisplayName();

private:
  struct ConnectResult {
    neurosdk_error_e error = NeuroSDK_None;
    neurosdk_context_t context{};
  };

  neurosdk_context_t ctx{};
  bool connectionReady = false;
  std::vector<Actions::Request> actionInbox;
  // neurosdk_context_create blocks for seconds while connecting, so it runs off the game thread.
  std::future<ConnectResult> pendingConnect;
  std::chrono::steady_clock::time_point nextConnectAttempt{};
  bool connectFailureLogged = false;

  void BeginConnect();
  void UpdateConnection();
  void DropContext();
  bool StartupMessage();
  bool SendSDKMessage(neurosdk_message_t &message);
  bool PollMessages();
  std::vector<Actions::Request> TakeActionInbox();
};
