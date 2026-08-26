
#include "NeuroSDK.hpp"
#include "Actions/ActionRegistry.hpp"
#include "CachedScripts.hpp"
#include "GameplayHandler.hpp"
#include "MenuHandler.hpp"
#include "Utils/DebugLog.hpp"
#include "WalkerHandler.hpp"
#include "common.hpp"
#include "neurosdk.h"
#include "nvse/CommandTable.h"
#include "nvse/GameObjects.h"
#include "nvse/PluginAPI.h"

CREATE_PLUGINSCRIPT(GetHeadingAngleBetweenPoints, float, callposx, float, callposy, float, callanglez, float, targetx,
                    float, targety);

CREATE_PLUGINSCRIPT(GetHeadingAngleAlt, float, callposx, float, callposy, float, callanglez, ref, target);

CREATE_PLUGINSCRIPT(SetGameSpeed, float, speed);

neurosdk_severity_e logSeverity = neurosdk_severity_e::NeuroSDK_Severity_Debug;

bool NeuroSDK::Initialize() {
  // Initialize the NeuroSDK context
  neurosdk_context_create_desc desc = {
      .url = NULL,
      .game_name = "Fallout: New Vegas",
      .poll_ms = 1, // This is blocking for somereason.. resulting in low fps. TODO: Figure out how to not have it block
      .flags =
          (neurosdk_context_create_flags_e)(neurosdk_context_create_flags_e::NeuroSDK_ContextCreateFlags_DebugPrints |
                                            neurosdk_context_create_flags_e::
                                                NeuroSDK_ContextCreateFlags_ValidationLayers),
      .callback_log =
          [](neurosdk_severity_e severity, char *message, void *user_data) {
            if (severity >= logSeverity) {
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
          },
      // #ifdef DEBUG
      // #endif
  };
  neurosdk_error_e err;
  if ((err = neurosdk_context_create(&ctx, &desc)) != NeuroSDK_None) {
    _MESSAGE("Failed to create NeuroSDK context: %d", err);
    return false;
  }

  isConnected = true;
  if (neurosdk_context_connected(&ctx))
    connectionReady = StartupMessage();
  return true;
}

void NeuroSDK::MainLoop() {
  if (!isConnected)
    return;

  if (!neurosdk_context_connected(&ctx)) {
    if (connectionReady) {
      connectionReady = false;
      ResetAutomation();
      _WARNING("NeuroSDK disconnected; local automation state was reset");
    }
    return;
  }

  if (!connectionReady) {
    if (!StartupMessage())
      return;
    connectionReady = true;
    _MESSAGE("NeuroSDK connected; startup and action registration are being restored");
  }

  if (!PollMessages()) {
    connectionReady = false;
    ResetAutomation();
    return;
  }

  const bool menuBlocksGameplay = MenuHandler::Process();
  GameplayHandler::Process(menuBlocksGameplay);
  Actions::ActionRegistry::Get().Dispatch(TakeActionInbox());

  if (menuBlocksGameplay)
    Walker::Pause();
  else
    Walker::Process();
}

void NeuroSDK::ResetAutomation() {
  MenuHandler::Reset();
  GameplayHandler::Reset();
  Actions::ActionRegistry::Get().Reset();
  actionInbox.clear();
  Walker::Stop();
}

bool NeuroSDK::StartupMessage() {
  neurosdk_message_t startupMessage{};
  startupMessage.kind = NeuroSDK_MessageKind_Startup;
  return SendSDKMessage(startupMessage);
}

bool NeuroSDK::SendContext(const char *message, bool silent) {
  if (!message || strlen(message) == 0) {
    _WARNING("Cannot send empty context message to NeuroSDK.");
    return false;
  }
  auto sdk = &NeuroSDK::GetSingleton();
  // _MESSAGE("Sending context message to NeuroSDK: %s and it is Silent: %b", message, silent);
  if (!sdk->connectionReady) {
    _WARNING("NeuroSDK is not connected. Cannot send context message.");
    return false;
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
  if (definitions.empty())
    return false;

  std::vector<neurosdk_action_t> actions;
  std::vector<std::string> schemas;
  actions.reserve(definitions.size());
  schemas.reserve(definitions.size());
  for (const auto &definition : definitions) {
    if (!definition.schema.IsEmpty() && definition.schema.GetType() != Actions::Json::JsonSchemaType::Object) {
      _WARNING("Cannot register action '%s': root schema type must be object", definition.name.c_str());
      return false;
    }
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
  if (names.empty())
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
  if (names.empty() || query.empty())
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
  if (id.empty())
    return false;
  _DMESSAGE("Sending action result (id: %s, success: %s): %s", id.c_str(), success ? "true" : "false",
            resultMessage.c_str());

  neurosdk_message_t message{};
  message.kind = NeuroSDK_MessageKind_ActionResult;
  message.value.action_result = {.id = const_cast<char *>(id.c_str()),
                                 .success = success,
                                 .message =
                                     resultMessage.empty() ? nullptr : const_cast<char *>(resultMessage.c_str())};
  return NeuroSDK::GetSingleton().SendSDKMessage(message);
}

bool NeuroSDK::SendSDKMessage(neurosdk_message_t &message) {
  if (!isConnected || !neurosdk_context_connected(&ctx))
    return false;
  const auto error = neurosdk_context_send(&ctx, &message);
  if (error != NeuroSDK_None) {
    _WARNING("Failed to send NeuroSDK message kind %d: %s", message.kind, neurosdk_error_string(error));
    return false;
  }

  // send() polls internally; drain any messages it received into plugin-owned storage.
  return PollMessages();
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
    if (message.kind != NeuroSDK_MessageKind_Action)
      continue;

    const auto &action = message.value.action;
    _DMESSAGE("Polled NeuroSDK action: %s (id: %s)", action.name ? action.name : "<missing>",
              action.id ? action.id : "<missing>");
    actionInbox.push_back({.id = action.id ? action.id : "",
                           .name = action.name ? action.name : "",
                           .data = action.data ? action.data : ""});
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
  if (!name) {
    _WARNING("Failed to get character display name from NeuroSDK.");
    return "";
  }
  return std::string(name);
}

// Command definitions
DEFINE_NEURO_COMMAND_PLUGIN(SendContext, "Send a context message to Neuro", false, kParams_OneString_OneOptionalInt);

bool Cmd_NSendContext_Execute(COMMAND_ARGS) {
  char message[2048] = {};
  BOOL silent = FALSE;
  ExtractArgsEx(EXTRACT_ARGS_EX, &message, &silent);
  NeuroSDK *neurosdk = &NeuroSDK::GetSingleton();
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
