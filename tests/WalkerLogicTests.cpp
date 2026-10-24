#include "Movement/WalkerLogic.hpp"
#include "TestFramework.hpp"
#include <cmath>
#include <limits>
#include <vector>

using namespace WalkerLogic;

namespace {

// Feeds a detector a straight walk for `seconds` at `speed` units/s sampled at `fps`; returns when it said stuck.
bool Walk(StuckDetector &detector, float seconds, float speed, float fps, float &x) {
  const float dt = 1.0f / fps;
  bool stuck = false;
  for (float t = 0.0f; t < seconds; t += dt) {
    x += speed * dt;
    stuck = detector.Update(dt, x, 0.0f, true) || stuck;
  }
  return stuck;
}

} // namespace

TEST("walker stuck: a walking player is never stuck, at any frame rate") {
  for (float fps : {10.0f, 30.0f, 60.0f, 144.0f}) {
    StuckDetector detector;
    float x = 0.0f;
    CHECK(!Walk(detector, 20.0f, 150.0f, fps, x));
  }
}

TEST("walker stuck: a stationary player is stuck after the window, at any frame rate") {
  for (float fps : {10.0f, 30.0f, 60.0f, 144.0f}) {
    StuckDetector detector;
    float x = 0.0f;
    CHECK(!Walk(detector, 1.2f, 0.0f, fps, x));
    CHECK(Walk(detector, 0.6f, 0.0f, fps, x));
  }
}

TEST("walker stuck: creeping a few units a second still counts as stuck") {
  StuckDetector detector;
  float x = 0.0f;
  CHECK(Walk(detector, 3.0f, 5.0f, 60.0f, x));
}

TEST("walker stuck: slow sneak-walking above the window distance is fine") {
  StuckDetector detector;
  float x = 0.0f;
  CHECK(!Walk(detector, 10.0f, 60.0f, 60.0f, x)); // 90 units per window
}

TEST("walker stuck: the verdict repeats only after a fresh window") {
  StuckDetector detector;
  float x = 0.0f;
  CHECK(Walk(detector, 2.0f, 0.0f, 60.0f, x));
  CHECK(!Walk(detector, 1.0f, 0.0f, 60.0f, x));
  CHECK(Walk(detector, 1.0f, 0.0f, 60.0f, x));
}

TEST("walker stuck: grace after a load or cell change suppresses the verdict") {
  StuckDetector detector;
  float x = 0.0f;
  detector.StartGrace(5.0f);
  CHECK(detector.InGrace());
  CHECK(!Walk(detector, 4.5f, 0.0f, 60.0f, x));
  CHECK(!Walk(detector, 1.0f, 0.0f, 60.0f, x)); // grace ends, history starts over
  CHECK(Walk(detector, 1.5f, 0.0f, 60.0f, x));
}

TEST("walker stuck: not moving never counts, and a teleport restarts the window") {
  StuckDetector detector;
  for (int i = 0; i < 600; ++i)
    CHECK(!detector.Update(0.05f, 0.0f, 0.0f, false));

  StuckDetector teleported;
  float x = 0.0f;
  CHECK(!Walk(teleported, 1.4f, 0.0f, 60.0f, x));
  CHECK(!teleported.Update(0.016f, 5000.0f, 0.0f, true)); // loaded into another place
  CHECK(!teleported.Update(0.016f, 5000.0f, 0.0f, true));
}

TEST("walker oscillation: flipping back and forth near one distance is detected") {
  OscillationDetector detector;
  bool fired = false;
  float distance = 300.0f;
  for (int i = 0; i < 40 && !fired; ++i) {
    distance += (i % 2 == 0) ? 6.0f : -6.0f;
    fired = detector.Update(0.1f, distance);
  }
  CHECK(fired);
}

TEST("walker oscillation: steady approach and non-finite distances do not fire") {
  OscillationDetector detector;
  float distance = 800.0f;
  for (int i = 0; i < 100; ++i) {
    distance -= 8.0f;
    CHECK(!detector.Update(0.1f, distance));
  }
  CHECK(!detector.Update(0.1f, std::numeric_limits<float>::infinity()));
}

TEST("walker progress: improving distance keeps resetting the stall timer") {
  ProgressTracker tracker;
  float remaining = 5000.0f;
  for (int i = 0; i < 2000; ++i) {
    remaining -= 3.0f; // 30 units per second beats the 25 units needed per 45 s
    CHECK(!tracker.Update(0.1f, remaining, true));
  }
}

TEST("walker progress: a walk that does not get closer stalls after the limit") {
  ProgressTracker tracker;
  bool stalled = false;
  float elapsed = 0.0f;
  for (; elapsed < 100.0f && !stalled; elapsed += 0.1f)
    stalled = tracker.Update(0.1f, 1000.0f + (static_cast<int>(elapsed) % 2) * 10.0f, true);
  CHECK(stalled);
  CHECK(elapsed > ProgressTracker::kStallSeconds - 0.5f && elapsed < ProgressTracker::kStallSeconds + 1.0f);
}

TEST("walker progress: leaving the counting state restarts it") {
  ProgressTracker tracker;
  for (int i = 0; i < 400; ++i)
    CHECK(!tracker.Update(0.1f, 1000.0f, true));
  CHECK(!tracker.Update(0.1f, 1000.0f, false));
  for (int i = 0; i < 400; ++i)
    CHECK(!tracker.Update(0.1f, 1000.0f, true));
}

TEST("walker recovery: the ladder escalates in order and skips Jump when it is unbound") {
  RecoveryPlanner planner;
  planner.Reset(12);
  auto first = planner.Next(true, false, 0, 0);
  auto second = planner.Next(true, false, 0, 0);
  auto third = planner.Next(true, false, 0, 0);
  auto fourth = planner.Next(true, false, 0, 0);
  auto fifth = planner.Next(true, false, 0, 0);
  REQUIRE(first && second && third && fourth && fifth);
  CHECK(first->step == RecoveryStep::Jump);
  CHECK(second->step == RecoveryStep::Sidestep && !second->mirrored);
  CHECK(third->step == RecoveryStep::Sidestep && third->mirrored);
  CHECK(fourth->step == RecoveryStep::BackOff);
  CHECK(fifth->step == RecoveryStep::Jump); // the ladder starts over

  RecoveryPlanner noJump;
  noJump.Reset(12);
  auto step = noJump.Next(false, false, 0, 0);
  REQUIRE(step);
  CHECK(step->step == RecoveryStep::Sidestep);
}

TEST("walker recovery: a door in front is tried first, once") {
  RecoveryPlanner planner;
  planner.Reset(12);
  auto door = planner.Next(true, true, 0, 0);
  REQUIRE(door);
  CHECK(door->step == RecoveryStep::Door);
  auto next = planner.Next(true, true, 0, 0);
  REQUIRE(next);
  CHECK(next->step == RecoveryStep::Jump); // the door did not help; do not keep opening and closing it
}

TEST("walker recovery: the last attempts before giving up wander to a new angle") {
  RecoveryPlanner planner;
  planner.Reset(RecoveryPlanner::kDefaultBudget);
  std::vector<RecoveryPlanner::Choice> choices;
  while (auto choice = planner.Next(true, false, 0, 0))
    choices.push_back(*choice);
  REQUIRE(static_cast<int>(choices.size()) == RecoveryPlanner::kDefaultBudget);
  const size_t count = choices.size();
  CHECK(choices[count - 1].step == RecoveryStep::Wander && choices[count - 1].tier == 1);
  CHECK(choices[count - 2].step == RecoveryStep::Wander && choices[count - 2].tier == 0);
  CHECK(choices[count - 3].step != RecoveryStep::Wander);
  CHECK(choices[0].step != RecoveryStep::Wander);
}

TEST("walker recovery: the budget runs out and quest routes get more") {
  RecoveryPlanner planner;
  planner.Reset(RecoveryPlanner::kDefaultBudget);
  for (int i = 0; i < RecoveryPlanner::kDefaultBudget; ++i)
    CHECK(planner.Next(true, false, 0, 0).has_value());
  CHECK(!planner.Next(true, false, 0, 0).has_value());
  CHECK(RecoveryPlanner::kQuestBudget > RecoveryPlanner::kDefaultBudget);

  RecoveryPlanner quest;
  quest.Reset(RecoveryPlanner::kQuestBudget);
  for (int i = 0; i < RecoveryPlanner::kQuestBudget; ++i)
    CHECK(quest.Next(true, false, 0, 0).has_value());
  CHECK(!quest.Next(true, false, 0, 0).has_value());
}

TEST("walker recovery: a tiny budget never wanders") {
  RecoveryPlanner planner;
  planner.Reset(2);
  CHECK(planner.Next(true, false, 0, 0)->step != RecoveryStep::Wander);
  CHECK(planner.Next(true, false, 0, 0)->step != RecoveryStep::Wander);
}

TEST("walker recovery: sliding back into the same corner never refills the budget") {
  RecoveryPlanner planner;
  planner.Reset(RecoveryPlanner::kDefaultBudget);
  int attempts = 0;
  // Stuck in the corner at (0, 0); every step frees the player for a few seconds, then it slides back in.
  for (int round = 0; round < 50; ++round) {
    if (!planner.Next(true, false, 0, 0))
      break;
    ++attempts;
    for (int frame = 0; frame < 300; ++frame) // 5 s at 60 fps, wandering 60 units around the corner
      planner.Update(1.0f / 60.0f, 60.0f * std::sin(frame * 0.05f), 40.0f);
  }
  CHECK_EQ(attempts, RecoveryPlanner::kDefaultBudget);
}

TEST("walker recovery: walking away for a clean stretch hands the budget back") {
  RecoveryPlanner planner;
  planner.Reset(RecoveryPlanner::kDefaultBudget);
  CHECK(planner.Next(true, false, 0, 0).has_value());
  CHECK(planner.Next(true, false, 0, 0).has_value());
  CHECK_EQ(planner.Attempts(), 2);
  // Far from the stuck place, but not long enough yet.
  for (int frame = 0; frame < 300; ++frame)
    planner.Update(1.0f / 60.0f, 1000.0f, 0.0f);
  CHECK_EQ(planner.Attempts(), 2);
  for (int frame = 0; frame < 400; ++frame)
    planner.Update(1.0f / 60.0f, 1000.0f, 0.0f);
  CHECK_EQ(planner.Attempts(), 0);
  auto restart = planner.Next(true, false, 1000, 0);
  REQUIRE(restart);
  CHECK(restart->step == RecoveryStep::Jump);
}

TEST("walker recovery: coming back near a stuck place restarts the clean stretch") {
  RecoveryPlanner planner;
  planner.Reset(RecoveryPlanner::kDefaultBudget);
  CHECK(planner.Next(true, false, 0, 0).has_value());
  for (int frame = 0; frame < 480; ++frame) // 8 s clean
    planner.Update(1.0f / 60.0f, 1000.0f, 0.0f);
  planner.Update(1.0f / 60.0f, 50.0f, 0.0f); // back at the corner
  for (int frame = 0; frame < 480; ++frame)
    planner.Update(1.0f / 60.0f, 1000.0f, 0.0f);
  CHECK_EQ(planner.Attempts(), 1); // 8 s again, not 16
}

TEST("walker recovery: detour offsets grow with the tier and cover every direction") {
  float dx = 0.0f;
  float dy = 0.0f;
  DetourOffset(0, 0.0f, dx, dy);
  const float near = std::hypot(dx, dy);
  DetourOffset(1, 0.0f, dx, dy);
  CHECK(std::hypot(dx, dy) > near);
  DetourOffset(0, 0.25f, dx, dy);
  CHECK(std::abs(dx) < 1.0f && dy > 0.0f);
  DetourOffset(0, 0.5f, dx, dy);
  CHECK(dx < 0.0f && std::abs(dy) < 1.0f);
  DetourOffset(0, 7.0f, dx, dy); // out of range input is clamped, not exploded
  CHECK(std::isfinite(dx) && std::isfinite(dy));
}

TEST("walker recovery: every step has a duration") {
  for (auto step :
       {RecoveryStep::Door, RecoveryStep::Jump, RecoveryStep::Sidestep, RecoveryStep::BackOff, RecoveryStep::Wander})
    CHECK(StepDuration(step) > 0.0f);
}

TEST("walker reminder: due after each full interval with cumulative numbers") {
  ProgressReminder reminder;
  int due = 0;
  ProgressReminder::Snapshot last;
  for (int i = 0; i < 1250; ++i) { // 125 s
    if (auto snapshot = reminder.Update(0.1f, 1.0f, false)) {
      ++due;
      last = *snapshot;
    }
  }
  CHECK_EQ(due, 3);
  CHECK(last.walkSeconds > 119.0f && last.walkSeconds < 121.0f);
  CHECK(last.walkedDistance > 1190.0f && last.walkedDistance < 1210.0f);
}

TEST("walker reminder: exploring reminds twice as often, and not before the first interval") {
  ProgressReminder reminder;
  for (int i = 0; i < 190; ++i)
    CHECK(!reminder.Update(0.1f, 0.0f, true).has_value());
  int due = 0;
  for (int i = 0; i < 1000; ++i)
    due += reminder.Update(0.1f, 0.0f, true).has_value() ? 1 : 0;
  CHECK_EQ(due, 5); // 119 s in total at a 20 s interval
}

TEST("walker pause notice: one note per episode after the threshold") {
  PauseNotice notice;
  int sent = 0;
  for (int i = 0; i < 400; ++i)
    sent += notice.Update(0.1f, true) ? 1 : 0; // 40 s
  CHECK_EQ(sent, 1);
  CHECK(!notice.Update(0.1f, false)); // resumed
  sent = 0;
  for (int i = 0; i < 400; ++i)
    sent += notice.Update(0.1f, true) ? 1 : 0;
  CHECK_EQ(sent, 1); // a new episode may report again
}

TEST("walker pause notice: a short pause says nothing") {
  PauseNotice notice;
  for (int i = 0; i < 200; ++i)
    CHECK(!notice.Update(0.1f, true)); // 20 s
}

TEST("walker messages: failures carry a cause and the blocking door") {
  const std::string stuck = DescribeFailure(FailureReason::Stuck, "the shack", "");
  CHECK(stuck.find("the shack") != std::string::npos && stuck.find("stuck") != std::string::npos);
  const std::string locked = DescribeFailure(FailureReason::DoorLocked, "the shack", "the door \"Old Door\"");
  CHECK(locked.find("Old Door") != std::string::npos && locked.find("locked") != std::string::npos);
  CHECK(DescribeFailure(FailureReason::TargetGone, "Easy Pete", "").find("no longer available") != std::string::npos);
  CHECK(DescribeFailure(FailureReason::WrongFloor, "the shelf", "").find("another floor") != std::string::npos);
  CHECK(DescribeFailure(FailureReason::NoProgress, "the shelf", "").find("closer") != std::string::npos);
  CHECK(DescribeFailure(FailureReason::ActivationRejected, "the safe", "").find("rejected") != std::string::npos);
  CHECK(DescribeFailure(FailureReason::Generic, "the safe", "") == "Could not reach the safe.");
}

TEST("walker messages: progress and pause texts name the target") {
  const std::string progress = DescribeProgress(true, false, "Goodsprings", 1200.0f, 61.0f, 3000.0f);
  CHECK(progress.find("quest") != std::string::npos && progress.find("Goodsprings") != std::string::npos);
  CHECK(progress.find("61 s") != std::string::npos);
  CHECK(DescribeProgress(false, true, "a ruin", 1.0f, 1.0f, 1.0f).find("exploring") != std::string::npos);
  const std::string pause = DescribePause("the saloon", "a menu is open");
  CHECK(pause.find("the saloon") != std::string::npos && pause.find("a menu is open") != std::string::npos);
}
