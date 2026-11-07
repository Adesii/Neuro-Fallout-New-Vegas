#pragma once

// Engine-independent Walker policy (todos 25 and 26): stuck and oscillation detection over time windows, the recovery
// escalation ladder, the progress-stall tracker, the walk progress reminder, the paused-command notice, and the text
// of failures. WalkerHandler feeds these engine reads and applies what they decide, like CombatHandler does with
// src/Combat/CombatLogic.*. Everything here depends on seconds, never on frame counts.

#include <deque>
#include <optional>
#include <string>

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
// whatever the stuck detector says (circling, a detour that never ends, a target on an unreachable ledge). It keeps
// counting through recovery steps: a walk that only ever recovers is not getting anywhere either. The designer prefers
// a walk that fails (Neuro then tries something else) to one that keeps running into a wall on stream (2026-11-07).
class ProgressTracker {
public:
  static constexpr float kStallSeconds = 30.0f;
  static constexpr float kMinImprovement = 70.0f; // about a meter

  void Reset();
  // Time only counts while `counting`; leaving the counting state restarts the tracker. True when stalled.
  bool Update(float dt, float remaining, bool counting);

private:
  float m_best = 0.0f;
  float m_idle = 0.0f;
  bool m_initialized = false;
};

// Running into an obstacle at an angle: the player slides along it, so it covers too much ground for StuckDetector but
// hardly moves where the walker is steering. Over a full window, the distance made along the wanted direction must be
// at least `kMinEfficiency` of the distance actually covered (pushing into a wall `a` degrees off its normal slides at
// about sin(a) efficiency, so 0.5 catches anything steeper than 30 degrees; a normal walk with 8-way movement keys
// stays above 0.9). Frames covering almost nothing are left to StuckDetector.
class SlideDetector {
public:
  static constexpr float kWindowSeconds = 2.0f;
  static constexpr float kMinEfficiency = 0.5f;
  static constexpr float kMinCovered = 50.0f; // below this in a window StuckDetector decides

  void Reset();
  // One frame: (dx, dy) is what the player moved this frame, (wantX, wantY) the direction the walker steers (any
  // length). `moving` is whether the walker wants to move. True when the window shows sliding; the history is cleared.
  bool Update(float dt, float dx, float dy, float wantX, float wantY, bool moving);

private:
  struct Sample {
    float time;
    float covered;
    float along;
  };
  std::deque<Sample> m_samples;
  float m_clock = 0.0f;
  float m_start = -1.0f;
};

// --- Interaction trigger ----------------------------------------------------------------------------------------

// When to activate a target the player is already within reach of. Walker activates the reference directly, so the
// crosshair never has to rest on it: an off-centre hitbox (a mailbox seen from the side) does not matter. Once the aim
// is done and the line of sight is clear it waits `kDelay` and interacts. The aim is done when the view is on the
// target, or when the view has stopped moving for `kSettleSeconds` (the turn finished short of the tolerance). A moving
// actor is never waited for: only the ray. Whatever holds it up, after `kGiveUpSeconds` within reach it interacts
// anyway and lets the game accept or reject the activation.
class InteractionGate {
public:
  static constexpr float kDelay = 1.0f;
  static constexpr float kActorDelay = 0.3f;
  static constexpr float kGiveUpSeconds = 5.0f;
  static constexpr float kSettleSeconds = 0.25f;
  static constexpr float kSettleRate = 0.05f; // radians per second of aim error change

  enum class Verdict {
    Wait,
    Interact, // aim done, ray clear, delay over
    GiveUp,   // within reach for kGiveUpSeconds without that happening: interact anyway
  };

  // Call when the player leaves reach or the route changes.
  void Reset();
  // One frame within reach. `aimError` is the remaining view error (heading plus pitch, radians) and `aligned` whether
  // it is within the walker's tolerance. `movingTarget` is an actor, whose view is not waited for.
  Verdict Update(float dt, bool aligned, float aimError, bool lineOfSight, bool movingTarget);
  // Whether the last Update judged the aim done (for the give-up log line).
  bool AimDone() const { return m_aimDone; }

private:
  float m_lastError = 0.0f;
  float m_still = 0.0f;
  float m_ready = 0.0f;
  float m_within = 0.0f;
  bool m_hasError = false;
  bool m_aimDone = false;
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
// The budget only comes back when the player got `kRefillProgress` closer to the target than it was at its closest
// stuck point: the obstacle is then really behind it. Time or distance walked never refills it: sliding out of a
// corner and back in kept refilling the budget both ways and the recovery looped forever (playtests 2026-10-25 and
// 2026-11-07).
class RecoveryPlanner {
public:
  static constexpr int kDefaultBudget = 6;
  static constexpr int kQuestBudget = 8;
  static constexpr int kWanderAttempts = 2;
  static constexpr float kRefillProgress = 700.0f; // 10 meters

  struct Choice {
    RecoveryStep step = RecoveryStep::Sidestep;
    bool mirrored = false; // sidestep to the left instead of the right
    int tier = 0;          // Wander: 0 first, 1 second
  };

  void Reset(int budget);
  // Next step, or nothing when the budget is spent. `canJump` is whether a Jump control is bound; `doorAvailable`
  // whether a closed unlocked door stands in front of the player; `targetDistance` the distance to the target where it
  // is stuck.
  std::optional<Choice> Next(bool canJump, bool doorAvailable, float targetDistance);
  // Every frame the walker moves toward the target, with the distance still to go.
  void Update(float remaining);
  int Attempts() const { return m_attempts; }

private:
  int m_budget = kDefaultBudget;
  int m_attempts = 0;
  int m_ladder = 0;
  bool m_doorTried = false;
  float m_closestStuck = 0.0f; // smallest remaining distance at which the player was judged stuck
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
