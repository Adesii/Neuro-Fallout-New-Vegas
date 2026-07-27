#pragma once
#include "nvse/CommandTable.h"
// Shortcut macro to register a script command (assigning it an Opcode).
#define RegisterScriptCommand(nvse, name) nvse->RegisterCommand(&kCommandInfo_##name)

// Short version of RegisterScriptCommand.
#define REG_CMD(nvse, name) RegisterScriptCommand(nvse, N##name) // Prefix Neuro

// Use this when the function's return type is not a number (when registering array/form/string functions).
// Credits: taken from JohnnyGuitarNVSE.
#define REG_TYPED_CMD(nvse, name, type) nvse->RegisterTypedCommand(&kCommandInfo_N##name, kRetnType_##type)

extern bool (*ExtractArgsEx)(COMMAND_ARGS_EX, ...);

#define DEFINE_NEURO_COMMAND_PLUGIN(name, description, isConsoleCommand, params)                                       \
  DEFINE_COMMAND_PLUGIN(N##name, description, isConsoleCommand, params)
