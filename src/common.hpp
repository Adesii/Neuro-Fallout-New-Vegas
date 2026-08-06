#pragma once
// 1. Force the universal standard types and foundational types first
// to fix the GameAPI.h "unknown override specifier" error
#include <cstdint>
#include <string>

// 2. Include the fundamental Windows wrappers cleanly
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "nvse/CommandTable.h"
#include "nvse/GameObjects.h"
#include "nvse/PluginAPI.h"
// Shortcut macro to register a script command (assigning it an Opcode).
#define RegisterScriptCommand(nvse, name) nvse->RegisterCommand(&kCommandInfo_##name)

// Short version of RegisterScriptCommand.
#define REG_CMD(nvse, name) RegisterScriptCommand(nvse, N##name) // Prefix Neuro

// Use this when the function's return type is not a number (when registering array/form/string functions).
// Credits: taken from JohnnyGuitarNVSE.
#define REG_TYPED_CMD(nvse, name, type) nvse->RegisterTypedCommand(&kCommandInfo_N##name, kRetnType_##type)

extern NVSEMessagingInterface *g_messagingInterface;
extern NVSEInterface *g_nvseInterface;
extern NVSECommandTableInterface *g_commandTableInterface;

#if RUNTIME
extern NVSEScriptInterface *g_script;
extern NVSEStringVarInterface *g_stringInterface;
extern NVSEArrayVarInterface *g_arrayInterface;
extern NVSEDataInterface *g_dataInterface;
extern NVSESerializationInterface *g_serializationInterface;
extern NVSEConsoleInterface *g_consoleInterface;
extern NVSEEventManagerInterface *g_eventInterface;
extern NVSETogglePlayerControlsInterface *g_togglePlayerControlsInterface;
extern bool (*ExtractArgsEx)(COMMAND_ARGS_EX, ...);
#endif

#define DEFINE_NEURO_COMMAND_PLUGIN(name, description, isConsoleCommand, params)                                       \
  DEFINE_COMMAND_PLUGIN(N##name, description, isConsoleCommand, params)
