
#include "NeuroSDK.hpp"
#include "Actions/ActionRegistry.hpp"
#include "Actions/TransportRules.hpp"
#include "CachedScripts.hpp"
#include "CombatHandler.hpp"
#include "Context/ContextGate.hpp"
#include "GameplayHandler.hpp"
#include "MenuHandler.hpp"
#include "Menus/PipBoyHandler.hpp"
#include "StanceHandler.hpp"
#include "Utils/DebugLog.hpp"
#include "WalkerHandler.hpp"
#include "common.hpp"
#include "hooks/Hooks_DirectInput8Create.h"
#include "neurosdk.h"
#include "nvse/CommandTable.h"
#include "nvse/GameObjects.h"
#include "nvse/PluginAPI.h"
#include <cstring>
#include <mutex>
#include <utility>

CREATE_PLUGINSCRIPT(GetHeadingAngleBetweenPoints, float, callposx, float, callposy, float, callanglez, float, targetx,
                    float, targety);

CREATE_PLUGINSCRIPT(GetHeadingAngleAlt, float, callposx, float, callposy, float, callanglez, ref, target);

CREATE_PLUGINSCRIPT(SetGameSpeed, float, speed);

namespace {

#ifdef _DEBUG
constexpr neurosdk_severity_e kLogSeverity = neurosdk_severity_e::NeuroSDK_Severity_Debug;
constexpr int kContextFlags = neurosdk_context_create_flags_e::NeuroSDK_ContextCreateFlags_DebugPrints |
                              neurosdk_context_create_flags_e::NeuroSDK_ContextCreateFlags_ValidationLayers;
#else
constexpr neurosdk_severity_e kLogSeverity = neurosdk_severity_e::NeuroSDK_Severity_Info;
constexpr int kContextFlags = 0;
#endif

constexpr auto kReconnectDelay = std::chrono::seconds(10);

char DEFAULT_EMPTY[] = "";

Context::ContextGate g_contextGate;

DWORD g_mainThreadId = 0;
std::mutex g_deferredLogMutex;
std::vector<std::pair<neurosdk_severity_e, std::string>> g_deferredLogs;

void EmitLog(neurosdk_severity_e severity, const char *message) {
  if (severity == NeuroSDK_Severity_Error) {
    _ERROR("[NeuroSDK] %s", message);
  } else if (severity == NeuroSDK_Severity_Warn) {
    _WARNING("[NeuroSDK] %s", message);
  } else if (severity == NeuroSDK_Severity_Info) {
    _MESSAGE("[NeuroSDK] %s", message);
  } else if (severity == NeuroSDK_Severity_Debug) {
    _DMESSAGE("[NeuroSDK] %s", message);
  }
}

// libneurosdk logs from whichever thread called it; the game log is only written from the game thread.
void LogCallback(neurosdk_severity_e severity, char *message, void *) {
  if (!message || severity < kLogSeverity)
    return;
  // Emitted once per frame by libneurosdk's poll; it drowns every other line.
  if (std::strstr(message, "Polling context for new messages"))
    return;
  if (GetCurrentThreadId() == g_mainThreadId) {
    EmitLog(severity, message);
    return;
  }
  std::lock_guard lock(g_deferredLogMutex);
  g_deferredLogs.emplace_back(severity, message);
}

void FlushDeferredLogs() {
  std::vector<std::pair<neurosdk_severity_e, std::string>> logs;
  {
    std::lock_guard lock(g_deferredLogMutex);
    logs.swap(g_deferredLogs);
  }
  for (const auto &[severity, message] : logs)
    EmitLog(severity, message.c_str());
}

} // namespace

bool NeuroSDK::Initialize() {
  g_mainThreadId = GetCurrentThreadId();
  BeginConnect();
  return true;
}

void NeuroSDK::BeginConnect() {
  pendingConnect = std::async(std::launch::async, []() {
    neurosdk_context_create_desc desc = {
        .url = NULL,
        .game_name = "Fallout: New Vegas",
        .poll_ms = 0, // Non-blocking: every poll and send would otherwise stall the frame.
        .flags = static_cast<neurosdk_context_create_flags_e>(kContextFlags),
        .callback_log = LogCallback,
    };
    ConnectResult result;
    result.error = neurosdk_context_create(&result.context, &desc);
    return result;
  });
}

void NeuroSDK::UpdateConnection() {
  if (pendingConnect.valid()) {
    if (pendingConnect.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return;
    auto result = pendingConnect.get();
    FlushDeferredLogs();
    if (result.error != NeuroSDK_None || !result.context) {
      if (!connectFailureLogged)
        _WARNING("Could not connect to NeuroSDK (%s); retrying every %lld s", neurosdk_error_string(result.error),
                 static_cast<long long>(kReconnectDelay.count()));
      connectFailureLogged = true;
      nextConnectAttempt = std::chrono::steady_clock::now() + kReconnectDelay;
      return;
    }
    connectFailureLogged = false;
    ctx = result.context;
    connectionReady = false;
    return;
  }
  if (std::chrono::steady_clock::now() >= nextConnectAttempt)
    BeginConnect();
}

void NeuroSDK::DropContext() {
  if (ctx)
    neurosdk_context_destroy(&ctx);
  ctx = {};
  nextConnectAttempt = std::chrono::steady_clock::now() + kReconnectDelay;
}

void NeuroSDK::MainLoop() {
  FlushDeferredLogs();
  if (!ctx) {
    UpdateConnection();
    return;
  }

  if (!neurosdk_context_connected(&ctx)) {
    // libneurosdk never reconnects a closed socket; a fresh context is the only way back.
    if (connectionReady) {
      connectionReady = false;
      ResetAutomation(true);
    }
    _WARNING("NeuroSDK disconnected; local automation state was reset and a reconnect will be attempted");
    DropContext();
    return;
  }

  if (!connectionReady) {
    if (!StartupMessage())
      return;
    connectionReady = true;
    g_contextGate.Connected();
    // The first connection of a session gets no status (the greeting is todo 24); a reconnect does.
    freshStatusPending = hasConnectedBefore;
    hasConnectedBefore = true;
    _MESSAGE("NeuroSDK connected; startup and action registration are being restored");
  }

  if (!PollMessages()) {
    connectionReady = false;
    ResetAutomation(true);
    DropContext();
    return;
  }

  SendFreshStatus();

  const bool menuBlocksGameplay = MenuHandler::Process();
  GameplayHandler::Process(menuBlocksGameplay);
  CombatHandler::Process(menuBlocksGameplay);
  StanceHandler::Process(menuBlocksGameplay);
  Actions::ActionRegistry::Get().Dispatch(TakeActionInbox());

  // Opening is accepted after menu observation. Pause Walker on that same frame,
  // including the short delay before the native opening animation begins.
  if (menuBlocksGameplay || Menus::PipBoyHandler::IsExecuting())
    Walker::Pause();
  else
    Walker::Process();
}

void NeuroSDK::ResetAutomation(bool connectionLost) {
  MenuHandler::Reset();
  GameplayHandler::Reset();
  CombatHandler::Reset();
  StanceHandler::Reset();
  Actions::ActionRegistry::Get().Reset();
  if (connectionLost)
    Actions::ActionRegistry::Get().DropPendingResults();
  actionInbox.clear();
  Walker::Stop();
  // Last: whatever an owner failed to release itself (disconnect, load, new game) must not stay pressed.
  DIHookControl::GetSingleton().ClearAllScripted();
}

bool NeuroSDK::StartupMessage() {
  neurosdk_message_t startupMessage{};
  startupMessage.kind = NeuroSDK_MessageKind_Startup;
  return SendSDKMessage(startupMessage);
}

void NeuroSDK::SendFreshStatus() {
  if (!freshStatusPending)
    return;
  // Empty until a game is loaded; the status then waits instead of describing the main menu.
  const std::string status = GameplayHandler::BuildFreshStatus();
  if (status.empty())
    return;
  if (SendContext(status.c_str(), false)) {
    _MESSAGE("Sent the status after reconnecting to Neuro");
    freshStatusPending = false;
  }
}

bool NeuroSDK::SendContext(const char *message, bool silent) {
  if (!message || strlen(message) == 0) {
    _WARNING("Cannot send empty context message to NeuroSDK.");
    return false;
  }
  auto sdk = &NeuroSDK::GetSingleton();
  if (!sdk->connectionReady) {
    // Context from before a reconnect would be stale, so it is dropped. Callers that clear state on success see false.
    if (g_contextGate.ShouldWarnOutage())
      _WARNING("NeuroSDK is not connected; context messages are dropped until it reconnects.");
    return false;
  }
  switch (g_contextGate.Check(message, silent)) {
  case Context::ContextGate::Verdict::Empty:
    _WARNING("Cannot send blank context message to NeuroSDK.");
    return false;
  case Context::ContextGate::Verdict::Duplicate:
    _DMESSAGE("Dropped a context message repeated within %lld s",
              static_cast<long long>(Context::ContextGate::kDuplicateWindow.count()));
    return true;
  case Context::ContextGate::Verdict::Send:
    break;
  }

  neurosdk_message_t context_message{};
  context_message.kind = NeuroSDK_MessageKind_Context;
  context_message.value = {.context = {
                               .message = const_cast<char *>(message),
                               .silent = silent,
                           }};

  _DMESSAGE("Sending context message: %s", message);
  return sdk->SendSDKMessage(context_message);
}

bool NeuroSDK::RegisterActions(const std::vector<Actions::Definition> &definitions) {
  std::string reason;
  if (!Actions::Transport::CanRegister(definitions, &reason)) {
    _WARNING("Cannot register actions: %s", reason.c_str());
    return false;
  }

  std::vector<neurosdk_action_t> actions;
  std::vector<std::string> schemas;
  actions.reserve(definitions.size());
  schemas.reserve(definitions.size());
  for (const auto &definition : definitions) {
    _DMESSAGE("Registering NeuroSDK action: %s", definition.name.c_str());
    schemas.push_back(definition.schema.Serialize());
    actions.push_back({.name = const_cast<char *>(definition.name.c_str()),
                       .description = const_cast<char *>(definition.description.c_str()),
                       .json_schema = const_cast<char *>(schemas.back().c_str())});
  }

  neurosdk_message_t message{};
  message.kind = NeuroSDK_MessageKind_ActionsRegister;
  message.value.actions_register = {.actions = actions.data(), .actions_len = static_cast<int>(actions.size())};
  return NeuroSDK::GetSingleton().SendSDKMessage(message);
}

bool NeuroSDK::UnregisterActions(const std::vector<std::string> &names) {
  if (Actions::Transport::IsNoopUnregister(names))
    return true;

  std::vector<char *> actionNames;
  actionNames.reserve(names.size());
  for (const auto &name : names)
    actionNames.push_back(const_cast<char *>(name.c_str()));
  for (const auto &name : names)
    _DMESSAGE("Unregistering NeuroSDK action: %s", name.c_str());

  neurosdk_message_t message{};
  message.kind = NeuroSDK_MessageKind_ActionsUnregister;
  message.value.actions_unregister = {.action_names = actionNames.data(),
                                      .action_names_len = static_cast<int>(actionNames.size())};
  return NeuroSDK::GetSingleton().SendSDKMessage(message);
}

bool NeuroSDK::ForceActions(const std::vector<std::string> &names, const std::string &query, const std::string &state,
                            ActionPriority priority, bool ephemeralContext) {
  if (!Actions::Transport::CanForce(names, query))
    return false;
  _DMESSAGE("Forcing %zu NeuroSDK action(s): %s", names.size(), query.c_str());

  std::vector<char *> actionNames;
  actionNames.reserve(names.size());
  for (const auto &name : names)
    actionNames.push_back(const_cast<char *>(name.c_str()));

  neurosdk_priority_e sdkPriority = NeuroSDK_Priority_Low;
  switch (priority) {
  case ActionPriority::Medium:
    sdkPriority = NeuroSDK_Priority_Medium;
    break;
  case ActionPriority::High:
    sdkPriority = NeuroSDK_Priority_High;
    break;
  case ActionPriority::Critical:
    sdkPriority = NeuroSDK_Priority_Critical;
    break;
  default:
    break;
  }

  neurosdk_message_t message{};
  message.kind = NeuroSDK_MessageKind_ActionsForce;
  message.value.actions_force = {.state = state.empty() ? nullptr : const_cast<char *>(state.c_str()),
                                 .query = const_cast<char *>(query.c_str()),
                                 .ephemeral_context = ephemeralContext,
                                 .action_names = actionNames.data(),
                                 .action_names_len = static_cast<int>(actionNames.size()),
                                 .priority = sdkPriority};
  return NeuroSDK::GetSingleton().SendSDKMessage(message);
}

bool NeuroSDK::SendActionResult(const std::string &id, bool success, const std::string &resultMessage) {
  if (!Actions::Transport::CanSendResult(id))
    return false;
  _DMESSAGE("Sending action result (id: %s, success: %s): %s", id.c_str(), success ? "true" : "false",
            resultMessage.c_str());

  neurosdk_message_t message{};
  message.kind = NeuroSDK_MessageKind_ActionResult;
  message.value.action_result = {.id = const_cast<char *>(id.c_str()),
                                 .success = success,
                                 .message =
                                     resultMessage.empty() ? DEFAULT_EMPTY : const_cast<char *>(resultMessage.c_str())};
  return NeuroSDK::GetSingleton().SendSDKMessage(message);
}

bool NeuroSDK::SendSDKMessage(neurosdk_message_t &message) {
  if (!ctx || !neurosdk_context_connected(&ctx))
    return false;
  const auto error = neurosdk_context_send(&ctx, &message);
  if (error != NeuroSDK_None) {
    _WARNING("Failed to send NeuroSDK message kind %d: %s", message.kind, neurosdk_error_string(error));
    return false;
  }

  // libneurosdk polls its socket while enqueueing. Incoming actions stay in its queue and are drained by MainLoop.
  return true;
}

bool NeuroSDK::PollMessages() {
  neurosdk_message_t *messages = nullptr;
  int count = 0;
  const auto error = neurosdk_context_poll(&ctx, &messages, &count);
  if (error != NeuroSDK_None) {
    _WARNING("Failed to poll NeuroSDK context: %s", neurosdk_error_string(error));
    return false;
  }

  for (int index = 0; index < count; ++index) {
    auto &message = messages[index];
    if (message.kind == NeuroSDK_MessageKind_Action) {
      const auto &action = message.value.action;
      _DMESSAGE("Polled NeuroSDK action: %s (id: %s)", action.name ? action.name : "<missing>",
                action.id ? action.id : "<missing>");
      actionInbox.push_back({.id = action.id ? action.id : "",
                             .name = action.name ? action.name : "",
                             .data = action.data ? action.data : ""});
    }
    // Every polled message is handed back, whatever its kind. The pinned libneurosdk only owns memory for Action
    // messages and returns NeuroSDK_UnknownCommand for the rest without freeing anything; a library update that
    // allocates for other kinds is then released without touching this loop.
    neurosdk_message_destroy(&message);
  }
  return true;
}

std::vector<Actions::Request> NeuroSDK::TakeActionInbox() {
  std::vector<Actions::Request> requests;
  requests.swap(actionInbox);
  return requests;
}

std::string NeuroSDK::GetCharacterDisplayName() {
  auto &sdk = NeuroSDK::GetSingleton();
  auto name = neurosdk_context_character_display_name(&sdk.ctx);
  static bool warned = false;
  if (!name) {
    // Callers poll this every frame while a prompt is open.
    if (!warned)
      _WARNING("Failed to get character display name from NeuroSDK.");
    warned = true;
    return "";
  }
  warned = false;
  return std::string(name);
}

// Command definitions
DEFINE_NEURO_COMMAND_PLUGIN(SendContext, "Send a context message to Neuro", false, kParams_OneString_OneOptionalInt);

bool Cmd_NSendContext_Execute(COMMAND_ARGS) {
  char message[2048] = {};
  BOOL silent = FALSE;
  ExtractArgsEx(EXTRACT_ARGS_EX, &message, &silent);
  NeuroSDK *neurosdk = NeuroSDK::GetSingletonPtr();
  if (!neurosdk) {
    _WARNING("NeuroSDK singleton is not initialized. Cannot send context message.");
    return false;
  }
  neurosdk->SendContext(message, silent);
  // _MESSAGE("Executing SendContext command with message: %s", message);
  return true;
}
void NeuroSDK::RegisterCommands(NVSEInterface *nvse) {
  // Register all commands here
  REG_CMD(nvse, SendContext);
}
