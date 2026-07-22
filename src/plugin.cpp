#include "common/IDebugLog.h"
#include <cstddef>
#include <neurosdk.h>
#include <nvse/PluginAPI.h>

#ifdef _WIN32
#define NEURO_FNV_EXPORT __declspec(dllexport)
#else
#define NEURO_FNV_EXPORT
#endif

IDebugLog gLog("neuro-fnv.log");
PluginHandle g_pluginHandle = kPluginHandle_Invalid;
NVSEMessagingInterface *g_messagingInterface{};
NVSEInterface *g_nvseInterface{};
NVSECommandTableInterface *g_commandTableInterface{};

NVSEScriptInterface *g_script{};
NVSEStringVarInterface *g_stringInterface{};
NVSEArrayVarInterface *g_arrayInterface{};
NVSEDataInterface *g_dataInterface{};
NVSESerializationInterface *g_serializationInterface{};
NVSEConsoleInterface *g_consoleInterface{};
NVSEEventManagerInterface *g_eventInterface{};
bool (*ExtractArgsEx)(COMMAND_ARGS_EX, ...);

neurosdk_context_t g_neuroContext;

void MessageHandler(NVSEMessagingInterface::Message *msg) {
  switch (msg->type) {
  case NVSEMessagingInterface::kMessage_PostLoad:
    break;
  case NVSEMessagingInterface::kMessage_ExitGame:
    break;
  case NVSEMessagingInterface::kMessage_ExitToMainMenu:
    break;
  case NVSEMessagingInterface::kMessage_LoadGame:
    break;
  case NVSEMessagingInterface::kMessage_SaveGame:
    break;
#if EDITOR
  case NVSEMessagingInterface::kMessage_ScriptEditorPrecompile:
    break;
#endif
  case NVSEMessagingInterface::kMessage_PreLoadGame:
    break;
  case NVSEMessagingInterface::kMessage_ExitGame_Console:
    break;
  case NVSEMessagingInterface::kMessage_PostLoadGame:
    break;
  case NVSEMessagingInterface::kMessage_PostPostLoad:
    break;
  case NVSEMessagingInterface::kMessage_RuntimeScriptError:
    break;
  case NVSEMessagingInterface::kMessage_DeleteGame:
    break;
  case NVSEMessagingInterface::kMessage_RenameGame:
    break;
  case NVSEMessagingInterface::kMessage_RenameNewGame:
    break;
  case NVSEMessagingInterface::kMessage_NewGame:
    break;
  case NVSEMessagingInterface::kMessage_DeleteGameName:
    break;
  case NVSEMessagingInterface::kMessage_RenameGameName:
    break;
  case NVSEMessagingInterface::kMessage_RenameNewGameName:
    break;
  case NVSEMessagingInterface::kMessage_DeferredInit:
    break;
  case NVSEMessagingInterface::kMessage_ClearScriptDataCache:
    break;
  case NVSEMessagingInterface::kMessage_MainGameLoop:
    break;
  case NVSEMessagingInterface::kMessage_ScriptCompile:
    break;
  case NVSEMessagingInterface::kMessage_EventListDestroyed:
    break;
  case NVSEMessagingInterface::kMessage_PostQueryPlugins:
    break;
  default:
    break;
  }
}

extern "C" NEURO_FNV_EXPORT bool NVSEPlugin_Query(const NVSEInterface *, PluginInfo *info) {
  _MESSAGE("neuro-fnv plugin query successful.");
  info->infoVersion = PluginInfo::kInfoVersion;
  info->name = "neuro-fnv";
  info->version = 1;
  return true;
}

extern "C" NEURO_FNV_EXPORT bool NVSEPlugin_Load(NVSEInterface *nvse) {
  _MESSAGE("neuro-fnv plugin loaded successfully.");
  g_pluginHandle = nvse->GetPluginHandle();
  g_nvseInterface = nvse;
  g_messagingInterface = (NVSEMessagingInterface *)nvse->QueryInterface(kInterface_Messaging);
  g_messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler);

  // script and function-related interfaces
  g_script = static_cast<NVSEScriptInterface *>(nvse->QueryInterface(kInterface_Script));
  g_stringInterface = static_cast<NVSEStringVarInterface *>(nvse->QueryInterface(kInterface_StringVar));
  g_arrayInterface = static_cast<NVSEArrayVarInterface *>(nvse->QueryInterface(kInterface_ArrayVar));
  g_dataInterface = static_cast<NVSEDataInterface *>(nvse->QueryInterface(kInterface_Data));
  g_eventInterface = static_cast<NVSEEventManagerInterface *>(nvse->QueryInterface(kInterface_EventManager));
  g_serializationInterface = static_cast<NVSESerializationInterface *>(nvse->QueryInterface(kInterface_Serialization));
  g_consoleInterface = static_cast<NVSEConsoleInterface *>(nvse->QueryInterface(kInterface_Console));
  ExtractArgsEx = g_script->ExtractArgsEx;

  // Initialize the NeuroSDK context
  neurosdk_context_create_desc desc = {
      .url = NULL,
      .game_name = "Fallout: New Vegas",
      .poll_ms = 1000,
      .callback_log = [](neurosdk_severity_e severity, char *message,
                         void *user_data) { _MESSAGE("[NeuroSDK] %s", message); },
#ifdef DEBUG
      .flags = NEUROSDK_CONTEXT_CREATE_FLAGS_DEBUG,
#endif
  };
  neurosdk_error_e err;
  if ((err = neurosdk_context_create(&g_neuroContext, &desc)) != NeuroSDK_None) {
    _MESSAGE("Failed to create NeuroSDK context: %d", err);
    return false;
  }

  neurosdk_message_t startup_message;
  startup_message.kind = NeuroSDK_MessageKind_Startup;
  if ((err = neurosdk_context_send(&g_neuroContext, &startup_message)) != NeuroSDK_None) {
    _MESSAGE("Failed to send startup message to NeuroSDK: %d", err);
    return false;
  }

  neurosdk_message_t random_context_message;
  random_context_message.kind = NeuroSDK_MessageKind_Context;
  random_context_message.value.context = {
      .message = (char *)"Hello From Fallout: New Vegas, Evil!",
      .silent = false,
  };
  if ((err = neurosdk_context_send(&g_neuroContext, &random_context_message)) != NeuroSDK_None) {
    _MESSAGE("Failed to send random context message to NeuroSDK: %d", err);
    return false;
  }

  return true;
}
