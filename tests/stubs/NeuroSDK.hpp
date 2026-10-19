#pragma once
// Host-test stand-in for src/NeuroSDK.hpp. It exposes the static transport surface the action layer uses;
// FakeNeuro.cpp implements it by recording calls. Nothing here talks to the engine or a socket.
#include "Actions/Action.hpp"
#include <string>
#include <vector>

class NeuroSDK {
public:
  enum class ActionPriority { Low, Medium, High, Critical };

  static bool SendContext(const char *message, bool silent = false);
  static bool RegisterActions(const std::vector<Actions::Definition> &definitions);
  static bool UnregisterActions(const std::vector<std::string> &names);
  static bool ForceActions(const std::vector<std::string> &names, const std::string &query, const std::string &state,
                           ActionPriority priority, bool ephemeralContext = false);
  static bool SendActionResult(const std::string &id, bool success, const std::string &message);
};
