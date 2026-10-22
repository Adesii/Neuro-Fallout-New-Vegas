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
#include "NeuroSDKTransport.inc"

  bool Initialize();
  void MainLoop();
  // connectionLost additionally drops results still owed for request ids of the dead session.
  void ResetAutomation(bool connectionLost = false);
  void RegisterCommands(NVSEInterface *nvse);

  static std::string GetCharacterDisplayName();

private:
  struct ConnectResult {
    neurosdk_error_e error = NeuroSDK_None;
    neurosdk_context_t context{};
  };

  neurosdk_context_t ctx{};
  bool connectionReady = false;
  // A status message is owed to Neuro: set when a connection becomes ready, cleared once it was sent.
  bool freshStatusPending = false;
  bool hasConnectedBefore = false;
  std::vector<Actions::Request> actionInbox;
  // neurosdk_context_create blocks for seconds while connecting, so it runs off the game thread.
  std::future<ConnectResult> pendingConnect;
  std::chrono::steady_clock::time_point nextConnectAttempt{};
  bool connectFailureLogged = false;

  void BeginConnect();
  void UpdateConnection();
  void DropContext();
  bool StartupMessage();
  void SendFreshStatus();
  bool SendSDKMessage(neurosdk_message_t &message);
  bool PollMessages();
  std::vector<Actions::Request> TakeActionInbox();
};
