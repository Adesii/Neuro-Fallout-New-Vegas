#pragma once

// Engine-independent Walker policy (todos 25 and 26): stuck and oscillation detection over time windows, the recovery
// escalation ladder, the progress-stall tracker, the walk progress reminder, the paused-command notice, and the text
// of failures. WalkerHandler feeds these engine reads and applies what they decide, like CombatHandler does with
// src/Combat/CombatLogic.*. Everything here depends on seconds, never on frame counts.

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace WalkerLogic {

// --- Why a command failed ---------------------------------------------------------------------------------------

enum class FailureReason {
  Generic,
  Stuck,              // physically blocked and every recovery step failed
  DoorLocked,         // a locked door stood in the way (detail = the door)
  NoProgress,         // kept moving but never got meaningfully closer
  TargetGone,         // the reference vanished (picked up, killed, unloaded)
  WrongFloor,         // within reach horizontally but not reachable from here (another level, or behind geometry)
  ActivationRejected, // the game refused the activation
  NotInteractable,
  NoWaypoint, // could not create the navigation helper
};

// A sentence for Neuro: "Could not reach X: ..." with the cause, or the cause alone for TargetGone.
// `target` is the command's description; `detail` names a blocking object when known.
std::string DescribeFailure(FailureReason reason, const std::string &target, const std::string &detail);

// Silent notice that a walk is on hold. `cause` is a short phrase such as "a menu is open".
std::string DescribePause(const std::string &target, const std::string &cause);

// --- Stuck detection --------------------------------------------------------------------------------------------

// Stuck means: wanted to move for a full window and covered less than `minDistance` in it. Skyrim's Walker uses 50
// units in 1.5 s; the same numbers, since a walking player covers well over 100 units in that time.
class StuckDetector {
public:
  static constexpr float kWindowSeconds = 1.5f;
  static constexpr float kMinDistance = 50.0f;
  // A jump this far between two frames is a teleport or a load, not walking.
  static constexpr float kTeleportDistance = 600.0f;

  void Reset();
  // Ignore stuck checks for this long (after a load, a cell change or a recovery step) and forget the history.
  void StartGrace(float seconds);
  bool InGrace() const { return m_grace > 0.0f; }
  // One frame. `moving` is whether the walker is trying to move this frame. True on the frame the player is judged
  // stuck; the history is then cleared so the next verdict needs a fresh window.
  bool Update(float dt, float x, float y, bool moving);

private:
  struct Sample {
    float time;
    float x;
    float y;
  };
  std::deque<Sample> m_samples;
  float m_clock = 0.0f;
  float m_grace = 0.0f;
};

// Walking back and forth about the same distance from the target (a path that keeps flipping): `reversalLimit`
// direction reversals inside one window while the distance never leaves a narrow band.
class OscillationDetector {
public:
  static constexpr float kWindowSeconds = 3.0f;
  static constexpr float kDistanceMargin = 50.0f;
  static constexpr float kDirectionThreshold = 2.0f;
  static constexpr int kReversalLimit = 6;

  void Reset();
  bool Update(float dt, float targetDistance);

private:
  float m_last = 0.0f;
  float m_window = 0.0f;
  float m_minimum = 0.0f;
  float m_maximum = 0.0f;
  int m_direction = 0;
  int m_reversals = 0;
  bool m_initialized = false;
};

// The remaining distance must improve by `kMinImprovement` within `kStallSeconds` or the walk is not getting anywhere,
// whatever the stuck detector says (circling, a detour that never ends, a target on an unreachable ledge).
class ProgressTracker {
public:
  static constexpr float kStallSeconds = 45.0f;
  static constexpr float kMinImprovement = 25.0f;

  void Reset();
  // Time only counts while `counting`; leaving the counting state restarts the tracker. True when stalled.
  bool Update(float dt, float remaining, bool counting);

private:
  float m_best = 0.0f;
  float m_idle = 0.0f;
  bool m_initialized = false;
};

// --- Recovery ladder --------------------------------------------------------------------------------------------

enum class RecoveryStep {
  Door,     // open a closed, unlocked door in front of the player
  Jump,     // hop while walking forward (rubble, low ledges)
  Sidestep, // walk sideways across the blocked line
  BackOff,  // walk backwards, then come at it again
  Wander,   // path to a random navmesh point nearby, to come at the obstacle from a new angle
};

// Seconds a step runs before the walker judges again (for Wander: the longest it may take to get there).
float StepDuration(RecoveryStep step);

// Where a Wander step goes, as an offset from the player. `tier` 0 is the first wander (a short hop away), 1 the second
// (further). `random01` in [0, 1) picks the direction; the caller supplies it and rejects spots that are not on the
// navmesh or not reachable, then asks again.
void DetourOffset(int tier, float random01, float &dx, float &dy);

// Chooses the next recovery step each time the player is judged stuck, and gives up when the budget is spent.
// Order: a door in front (once), then Jump, Sidestep (right), Sidestep (left), BackOff, repeating; the last
// `kWanderAttempts` attempts are Wander steps instead, so the walk tries a genuinely different angle right before it
// gives up. Every step ends with a fresh route search from wherever it left the player.
//
// The budget only comes back after the player has been walking for `kCleanSeconds` without being judged stuck and is
// at least `kProgressDistance` from every place it was stuck. Counting distance walked was wrong: sliding back into
// the same corner after a sidestep kept refilling it and the recovery looped forever.
class RecoveryPlanner {
public:
  static constexpr int kDefaultBudget = 6;
  static constexpr int kQuestBudget = 12;
  static constexpr int kWanderAttempts = 2;
  static constexpr float kProgressDistance = 200.0f;
  static constexpr float kCleanSeconds = 10.0f;

  struct Choice {
    RecoveryStep step = RecoveryStep::Sidestep;
    bool mirrored = false; // sidestep to the left instead of the right
    int tier = 0;          // Wander: 0 first, 1 second
  };

  void Reset(int budget);
  // Next step, or nothing when the budget is spent. `canJump` is whether a Jump control is bound; `doorAvailable`
  // whether a closed unlocked door stands in front of the player; (x, y) is where the player is stuck.
  std::optional<Choice> Next(bool canJump, bool doorAvailable, float x, float y);
  // Every frame the walker is going about its business (not recovering).
  void Update(float dt, float x, float y);
  int Attempts() const { return m_attempts; }

private:
  struct Spot {
    float x;
    float y;
  };
  int m_budget = kDefaultBudget;
  int m_attempts = 0;
  int m_ladder = 0;
  bool m_doorTried = false;
  float m_clean = 0.0f;
  std::vector<Spot> m_spots;
};

// --- Progress reminder ------------------------------------------------------------------------------------------

// Skyrim's walker reminds the model every 40 s of a walk (20 s while exploring): how far it walked, for how long and
// how far the target still is. Walker feeds the walked distance and time; Snapshot says when one is due.
class ProgressReminder {
public:
  static constexpr float kInterval = 40.0f;
  static constexpr float kExploreInterval = 20.0f;

  struct Snapshot {
    float walkedDistance = 0.0f;
    float walkSeconds = 0.0f;
  };

  void Reset();
  // `walked` is the distance covered since the last call. Nothing is due before the first full interval.
  std::optional<Snapshot> Update(float dt, float walked, bool exploring);
  // A fresh message just went out for other reasons (a status update); push the next reminder back.
  void Postpone() { m_sinceReminder = 0.0f; }

private:
  float m_sinceReminder = 0.0f;
  float m_walkedTotal = 0.0f;
  float m_secondsTotal = 0.0f;
};

std::string DescribeProgress(bool quest, bool exploring, const std::string &target, float walkedDistance,
                             float walkSeconds, float remainingDistance);

// --- Paused-command notice --------------------------------------------------------------------------------------

// A command sitting on hold for `kAfterSeconds` (a menu, disabled controls) is worth one silent note per episode, so
// the model knows why nothing is happening.
class PauseNotice {
public:
  static constexpr float kAfterSeconds = 30.0f;

  void Reset();
  // `paused` for this frame; true once when the pause crosses the threshold. Resuming starts a new episode.
  bool Update(float dt, bool paused);

private:
  float m_paused = 0.0f;
  bool m_sent = false;
};

} // namespace WalkerLogic
