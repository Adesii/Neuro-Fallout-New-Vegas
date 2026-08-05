#pragma once
#include "common/ISingleton.h"
#include "neurosdk.h"
#include "nvse/CommandTable.h"
#include "nvse/ParamInfos.h"
#include "nvse/PluginAPI.h"

#include "common.hpp"
class NeuroSDK : public ISingleton<NeuroSDK> {
public:
  bool Initialize();
  void MainLoop();
  static void SendContext(char *message, bool silent = false);
  void RegisterCommands(NVSEInterface *nvse);

private:
  neurosdk_context_t ctx;
  bool isConnected = false;

  void StartupMessage();
};
