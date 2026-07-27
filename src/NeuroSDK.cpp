#include "NeuroSDK.hpp"
#include "CachedScripts.hpp"
#include "common.hpp"
#include "common/IDebugLog.h"
#include "neurosdk.h"
#include "nvse/CommandTable.h"
#include "nvse/GameObjects.h"
#include "nvse/PluginAPI.h"

CREATE_PLUGINSCRIPT(GetHeadingAngleBetweenPoints, float, callposx, float, callposy, float, callanglez, float, targetx, float,
                    targety);

bool NeuroSDK::Initialize() {
  // Initialize the NeuroSDK context
  neurosdk_context_create_desc desc = {
      .url = NULL,
      .game_name = "Fallout: New Vegas",
      .poll_ms = 50,
      .callback_log = [](neurosdk_severity_e severity, char *message, void *user_data) { _MESSAGE("[NeuroSDK] %s", message); },
#ifdef DEBUG
      .flags = NEUROSDK_CONTEXT_CREATE_FLAGS_DEBUG,
#endif
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
  _MESSAGE("NeuroSDK MainLoop called.");
  PlayerCharacter *player = PlayerCharacter::GetSingleton();
  _MESSAGE("PlayerCharacter pointer: %p", player);
  if (!player) {
    _WARNING("PlayerCharacter is null. Cannot execute test command.");
    return;
  }
  NVSEArrayVarInterface::Element result;
  const bool ok =
      CachedScripts::Call(kGetHeadingAngleBetweenPointsScript, player, result, CachedScripts::FloatArg(player->posX),
                          CachedScripts::FloatArg(player->posY), CachedScripts::FloatArg(player->rotZ),
                          CachedScripts::FloatArg(player->posX + 100.0f), CachedScripts::FloatArg(player->posY + 100.0f));

  _MESSAGE("Executed test command with result: %s", ok ? "success" : "failure");
  if (ok) {
    const double angle = result.GetNumber();
    _MESSAGE("Executed test command with result: %f", angle);
  }
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
  // _MESSAGE("Sending context message to NeuroSDK: %s and it is Silent: %b", message, silent);
  if (!isConnected) {
    _WARNING("NeuroSDK is not connected. Cannot send context message.");
    return;
  }

  neurosdk_message_t context_message;
  context_message.kind = NeuroSDK_MessageKind_Context;
  context_message.value = {.context = {
                               .message = message,
                               .silent = silent,
                           }};

  neurosdk_error_e err;
  if ((err = neurosdk_context_send(&ctx, &context_message)) != NeuroSDK_None) {
    _WARNING("Failed to send context message to NeuroSDK: %d", err);
  }
}

// Command definitions
DEFINE_NEURO_COMMAND_PLUGIN(SendContext, "Send a context message to Neuro", false, kParams_OneString_OneOptionalInt);

bool Cmd_NSendContext_Execute(COMMAND_ARGS) {
  char message[2048] = {};
  _MESSAGE("Executing SendContext command with message: %s", message);
  BOOL silent = FALSE;
  ExtractArgsEx(EXTRACT_ARGS_EX, &message, &silent);
  NeuroSDK *neurosdk = &NeuroSDK::GetSingleton();
  if (!neurosdk) {
    _WARNING("NeuroSDK singleton is not initialized. Cannot send context message.");
    return false;
  }
  neurosdk->SendContext(message, silent);
  return true;
}
void NeuroSDK::RegisterCommands(NVSEInterface *nvse) {
  // Register all commands here
  REG_CMD(nvse, SendContext);
}
