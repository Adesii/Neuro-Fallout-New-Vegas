
#include "NeuroSDK.hpp"
#include "CachedScripts.hpp"
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

bool NeuroSDK::Initialize() {
  // Initialize the NeuroSDK context
  neurosdk_context_create_desc desc = {
      .url = NULL,
      .game_name = "Fallout: New Vegas",
      .poll_ms = 100,
      .flags =
          (neurosdk_context_create_flags_e)(neurosdk_context_create_flags_e::NeuroSDK_ContextCreateFlags_DebugPrints |
                                            neurosdk_context_create_flags_e::
                                                NeuroSDK_ContextCreateFlags_ValidationLayers),
      .callback_log = [](neurosdk_severity_e severity, char *message,
                         void *user_data) { _MESSAGE("[NeuroSDK] %s", message); },
      // #ifdef DEBUG
      // #endif
  };
  neurosdk_error_e err;
  if ((err = neurosdk_context_create(&ctx, &desc)) != NeuroSDK_None) {
    _MESSAGE("Failed to create NeuroSDK context: %d", err);
    return false;
  }

  isConnected = true;

  StartupMessage();

  return isConnected;
}

void NeuroSDK::MainLoop() {
  if (!isConnected) {
    _WARNING("NeuroSDK is not connected. Skipping MainLoop.");
    return;
  }

  neurosdk_message_t *messages = NULL;
  int count = 0;

  auto err = neurosdk_context_poll(&ctx, &messages, &count);
  if (err != NeuroSDK_None) {
    _WARNING("Failed to poll NeuroSDK context: %d", err);
    return;
  }

  MenuHandler::Process();
  Walker::Process();
}

void NeuroSDK::StartupMessage() {
  neurosdk_message_t startup_message;
  startup_message.kind = NeuroSDK_MessageKind_Startup;
  neurosdk_error_e err;
  if ((err = neurosdk_context_send(&ctx, &startup_message)) != NeuroSDK_None) {
    _ERROR("Failed to send startup message to NeuroSDK: %d", err);
  }
}

void NeuroSDK::SendContext(char *message, bool silent) {
  if (!message || strlen(message) == 0) {
    _WARNING("Cannot send empty context message to NeuroSDK.");
    return;
  }
  auto sdk = &NeuroSDK::GetSingleton();
  // _MESSAGE("Sending context message to NeuroSDK: %s and it is Silent: %b", message, silent);
  if (!sdk->isConnected) {
    _WARNING("NeuroSDK is not connected. Cannot send context message.");
    return;
  }

  neurosdk_message_t context_message;
  context_message.kind = NeuroSDK_MessageKind_Context;
  context_message.value = {.context = {
                               .message = message,
                               .silent = silent,
                           }};

  _DMESSAGE("Sending context message: %s", message);
  neurosdk_error_e err;
  if ((err = neurosdk_context_send(&sdk->ctx, &context_message)) != NeuroSDK_None) {
    _WARNING("Failed to send context message to NeuroSDK: %d", err);
  }
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
