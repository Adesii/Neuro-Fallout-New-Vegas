// The static transport surface of NeuroSDK. Included inside the class body by both src/NeuroSDK.hpp and the host-test
// stand-in tests/stubs/NeuroSDK.hpp, so the two cannot drift apart.
enum class ActionPriority { Low, Medium, High, Critical };

// Returns false when nothing was sent (disconnected, or the message violates a transport precondition).
static bool SendContext(const char *message, bool silent = false);
static bool RegisterActions(const std::vector<Actions::Definition> &definitions);
static bool UnregisterActions(const std::vector<std::string> &names);
static bool ForceActions(const std::vector<std::string> &names, const std::string &query, const std::string &state,
                         ActionPriority priority, bool ephemeralContext = false);
static bool SendActionResult(const std::string &id, bool success, const std::string &message);
