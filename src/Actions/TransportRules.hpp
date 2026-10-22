#pragma once

// Preconditions of the NeuroSDK transport, shared by the real transport (NeuroSDK.cpp) and the host-test fake so the
// fake cannot be more lenient than the real thing. Engine-free.
#include "Actions/Action.hpp"
#include <string>
#include <vector>

namespace Actions::Transport {

// libneurosdk rejects an empty list, and the server accepts only object root schemas (or none).
inline bool CanRegister(const std::vector<Definition> &definitions, std::string *reason = nullptr) {
  if (definitions.empty()) {
    if (reason)
      *reason = "no actions to register";
    return false;
  }
  for (const auto &definition : definitions) {
    if (!definition.schema.IsEmpty() && definition.schema.GetType() != Json::JsonSchemaType::Object) {
      if (reason)
        *reason = "root schema of '" + definition.name + "' must be an object";
      return false;
    }
  }
  return true;
}

// Unregistering nothing succeeds without sending anything.
inline bool IsNoopUnregister(const std::vector<std::string> &names) { return names.empty(); }

inline bool CanForce(const std::vector<std::string> &names, const std::string &query) {
  if (names.empty() || query.empty())
    return false;
  for (const auto &name : names) {
    if (name.empty())
      return false;
  }
  return true;
}

inline bool CanSendResult(const std::string &id) { return !id.empty(); }

} // namespace Actions::Transport
