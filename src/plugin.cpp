#include "GameplayHandler.hpp"
#include "NeuroSDK.hpp"
#include "Utils/DebugLog.hpp"
#include "common.hpp"
#include "hooks/Hooks_DirectInput8Create.h"
#include "nvse/GameObjects.h"
#include <neurosdk.h>
#include <nvse/PluginAPI.h>

#ifdef _WIN32
#define NEURO_FNV_EXPORT __declspec(dllexport)
#else
#define NEURO_FNV_EXPORT
#endif

IDebugLog gLog("neuro-fnv.log");
PluginHandle g_pluginHandle = kPluginHandle_Invalid;
NeuroSDK *g_neuroSDK = nullptr;
NVSEMessagingInterface *g_messagingInterface{};
NVSEInterface *g_nvseInterface{};
NVSECommandTableInterface *g_commandTableInterface{};

#if RUNTIME
NVSEScriptInterface *g_script{};
NVSEStringVarInterface *g_stringInterface{};
NVSEArrayVarInterface *g_arrayInterface{};
NVSEDataInterface *g_dataInterface{};
NVSESerializationInterface *g_serializationInterface{};
NVSEConsoleInterface *g_consoleInterface{};
NVSEEventManagerInterface *g_eventInterface{};
NVSETogglePlayerControlsInterface *g_togglePlayerControlsInterface{};
bool (*ExtractArgsEx)(COMMAND_ARGS_EX, ...);
#endif

void MessageHandler(NVSEMessagingInterface::Message *msg) {
  switch (msg->type) {
  case NVSEMessagingInterface::kMessage_PostLoad:
    break;
  case NVSEMessagingInterface::kMessage_ExitGame:
    break;
  case NVSEMessagingInterface::kMessage_ExitToMainMenu:
    GameplayHandler::SetReady(false);
    if (g_neuroSDK)
      g_neuroSDK->ResetAutomation();
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
    GameplayHandler::SetReady(false);
    if (g_neuroSDK)
      g_neuroSDK->ResetAutomation();
    break;
  case NVSEMessagingInterface::kMessage_ExitGame_Console:
    break;
  case NVSEMessagingInterface::kMessage_PostLoadGame:
    GameplayHandler::SetReady(msg->data != nullptr);
    if (g_neuroSDK && msg->data != nullptr)
      g_neuroSDK->RequestGreeting();
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
    if (g_neuroSDK)
      g_neuroSDK->ResetAutomation();
    GameplayHandler::SetReady(true);
    if (g_neuroSDK)
      g_neuroSDK->RequestGreeting();
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
    // _MESSAGE("Main Game Loop message received. Calling NeuroSDK MainLoop.");
    if (g_neuroSDK) {
      g_neuroSDK->MainLoop();
    }
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

extern "C" NEURO_FNV_EXPORT bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info) {
  info->infoVersion = PluginInfo::kInfoVersion;
  info->name = "neuro-fnv";
  info->version = 1;

  // Hooks and structure overlays use absolute addresses of one runtime build, and the DirectInput
  // hook patches the game's import table. Never load into the GECK or another game version.
  if (nvse->isEditor) {
    _ERROR("neuro-fnv does not support the GECK editor; plugin not loaded.");
    return false;
  }
  if (nvse->runtimeVersion != RUNTIME_VERSION) {
    _ERROR("neuro-fnv requires Fallout: New Vegas 1.4.0.525 (runtime %08X expected, %08X found); plugin not loaded.",
           static_cast<unsigned>(RUNTIME_VERSION), static_cast<unsigned>(nvse->runtimeVersion));
    return false;
  }
  _MESSAGE("neuro-fnv plugin query successful.");
  return true;
}

extern "C" NEURO_FNV_EXPORT bool NVSEPlugin_Load(NVSEInterface *nvse) {
  _MESSAGE("neuro-fnv plugin loaded successfully.");
  g_pluginHandle = nvse->GetPluginHandle();
  g_nvseInterface = nvse;
  g_messagingInterface = (NVSEMessagingInterface *)nvse->QueryInterface(kInterface_Messaging);
  g_commandTableInterface = static_cast<NVSECommandTableInterface *>(nvse->QueryInterface(kInterface_CommandTable));
  if (!g_messagingInterface) {
    _ERROR("xNVSE messaging interface is unavailable; plugin not loaded.");
    return false;
  }
  g_messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler);

  if (!nvse->isEditor) {
#if RUNTIME
    // script and function-related interfaces
    g_script = static_cast<NVSEScriptInterface *>(nvse->QueryInterface(kInterface_Script));
    g_stringInterface = static_cast<NVSEStringVarInterface *>(nvse->QueryInterface(kInterface_StringVar));
    g_arrayInterface = static_cast<NVSEArrayVarInterface *>(nvse->QueryInterface(kInterface_ArrayVar));
    g_dataInterface = static_cast<NVSEDataInterface *>(nvse->QueryInterface(kInterface_Data));
    g_eventInterface = static_cast<NVSEEventManagerInterface *>(nvse->QueryInterface(kInterface_EventManager));
    g_serializationInterface =
        static_cast<NVSESerializationInterface *>(nvse->QueryInterface(kInterface_Serialization));
    g_consoleInterface = static_cast<NVSEConsoleInterface *>(nvse->QueryInterface(kInterface_Console));
    g_togglePlayerControlsInterface =
        static_cast<NVSETogglePlayerControlsInterface *>(nvse->QueryInterface(kInterface_PlayerControls));
    if (!g_script) {
      _ERROR("xNVSE script interface is unavailable; plugin not loaded.");
      return false;
    }
    ExtractArgsEx = g_script->ExtractArgsEx;
#endif
  }

  g_neuroSDK = new NeuroSDK();

  UINT32 const neuroOpcodeBase = 0x4297;

  nvse->SetOpcodeBase(neuroOpcodeBase);

#if RUNTIME
  if (!nvse->isEditor) {
    if (!NeuroSDK::GetSingleton().Initialize())
      _MESSAGE("Failed to initialize NeuroSDK transport.");
    else
      _MESSAGE("NeuroSDK transport initialized.");
  }
#endif
  // Register commands
  NeuroSDK::GetSingleton().RegisterCommands(nvse);
// HOOKS
#if RUNTIME
  if (!nvse->isEditor)
    Hook_DirectInput8Create_Init();
#endif
  return true;
}
