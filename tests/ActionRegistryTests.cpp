#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "FakeNeuro.hpp"
#include "TestFramework.hpp"

using namespace Actions;
using Fake::Action;
using Fake::Log;
using Fake::State;

namespace {

Request MakeRequest(std::string id, std::string name, std::string data = "{}") {
  return {std::move(id), std::move(name), std::move(data)};
}

std::unique_ptr<IAction> LoggingAction(const std::string &name) {
  return std::make_unique<Action>(name, [](const Request &) {
    Log("validate");
    return PreparedAction::Success([] { Log("execute"); },
                                   []() -> std::optional<std::string> {
                                     Log("revalidate");
                                     return std::nullopt;
                                   });
  });
}

} // namespace

TEST("dispatch: validate, revalidate, close window, send result, then execute") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(LoggingAction("pick"));
  REQUIRE(window.Register());

  ActionRegistry::Get().Dispatch({MakeRequest("7", "pick")});

  const std::vector<std::string> expected = {"register:pick",   "validate",       "revalidate",
                                             "unregister:pick", "result:7:true:", "execute"};
  CHECK(State().log == expected);
  CHECK(window.GetState() == ActionWindow::State::Ended);
}

TEST("dispatch: unknown action gets a failed result and nothing runs") {
  Fake::ResetWorld();
  ActionRegistry::Get().Dispatch({MakeRequest("1", "nope")});
  REQUIRE(State().log.size() == 1);
  CHECK(State().log[0].starts_with("result:1:false:"));
  CHECK(State().log[0].find("nope") != std::string::npos);
}

TEST("dispatch: validation failure reports and keeps the window open for a retry") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("pick", [](const Request &) { return PreparedAction::Failure("bad index"); }));
  REQUIRE(window.Register());

  ActionRegistry::Get().Dispatch({MakeRequest("2", "pick")});

  CHECK(State().Contains("result:2:false:bad index"));
  CHECK(!State().Contains("unregister"));
  CHECK(window.GetState() == ActionWindow::State::Registered);
  // The same action is still bound, so a retry reaches validation again.
  ActionRegistry::Get().Dispatch({MakeRequest("3", "pick")});
  CHECK(State().Contains("result:3:false:bad index"));
}

TEST("dispatch: failed revalidation rejects without closing the window or executing") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("pick", [](const Request &) {
    return PreparedAction::Success([] { Log("execute"); },
                                   []() -> std::optional<std::string> { return "state moved on"; });
  }));
  REQUIRE(window.Register());

  ActionRegistry::Get().Dispatch({MakeRequest("4", "pick")});

  CHECK(State().Contains("result:4:false:state moved on"));
  CHECK_EQ(State().Count("execute"), size_t{0});
  CHECK(!State().Contains("unregister"));
}

TEST("dispatch: window that cannot close is retried, nothing executes, result is a failure") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(LoggingAction("pick"));
  REQUIRE(window.Register());

  State().failUnregister = true;
  ActionRegistry::Get().Dispatch({MakeRequest("5", "pick")});
  CHECK(State().Contains("result:5:false:"));
  CHECK_EQ(State().Count("execute"), size_t{0});
  CHECK(window.GetState() == ActionWindow::State::Closing);

  // The action was unbound locally, so a duplicate request cannot execute twice.
  ActionRegistry::Get().Dispatch({MakeRequest("6", "pick")});
  CHECK(State().Contains("result:6:false:Unknown"));

  State().failUnregister = false;
  CHECK(window.End());
  CHECK(window.GetState() == ActionWindow::State::Ended);
}

TEST("dispatch: failed result send defers execution until the result is delivered") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(LoggingAction("pick"));
  REQUIRE(window.Register());

  State().failResult = true;
  ActionRegistry::Get().Dispatch({MakeRequest("8", "pick")});
  CHECK_EQ(State().Count("execute"), size_t{0});
  CHECK(ActionRegistry::Get().HasPendingResult("pick"));

  State().failResult = false;
  ActionRegistry::Get().Dispatch({});
  CHECK_EQ(State().Count("execute"), size_t{1});
  CHECK(State().Contains("result:8:true:"));
  CHECK(!ActionRegistry::Get().HasPendingResult("pick"));
  // Revalidation ran again right before the deferred result was resent.
  CHECK_EQ(State().Count("revalidate"), size_t{2});
}

TEST("dispatch: deferred action whose revalidation now fails sends failure and never executes") {
  Fake::ResetWorld();
  static bool stillValid;
  stillValid = true;
  ActionWindow window;
  window.Add(std::make_unique<Action>("pick", [](const Request &) {
    return PreparedAction::Success([] { Log("execute"); },
                                   []() -> std::optional<std::string> {
                                     if (stillValid)
                                       return std::nullopt;
                                     return "gone";
                                   });
  }));
  REQUIRE(window.Register());

  State().failResult = true;
  ActionRegistry::Get().Dispatch({MakeRequest("9", "pick")});
  stillValid = false;
  State().failResult = false;
  ActionRegistry::Get().Dispatch({});

  CHECK(State().Contains("result:9:false:gone"));
  CHECK_EQ(State().Count("execute"), size_t{0});
}

TEST("dispatch: a second request while a result is pending is rejected") {
  Fake::ResetWorld();
  auto persistent = LoggingAction("pick");
  REQUIRE(ActionRegistry::Get().Bind(*persistent));

  // Pending results are flushed at the start of every dispatch, so the duplicate can only be seen
  // while the connection keeps refusing results.
  State().failResult = true;
  ActionRegistry::Get().Dispatch({MakeRequest("10", "pick")});
  ActionRegistry::Get().Dispatch({MakeRequest("11", "pick")});
  CHECK_EQ(State().Count("execute"), size_t{0});

  State().failResult = false;
  ActionRegistry::Get().Dispatch({});
  CHECK(State().Contains("result:10:true:"));
  CHECK(State().Contains("result:11:false:A previous request"));
  CHECK_EQ(State().Count("execute"), size_t{1});
  ActionRegistry::Get().Unbind(*persistent);
}

TEST("reset: pending accepted results become failures and never execute") {
  Fake::ResetWorld();
  auto persistent = LoggingAction("pick");
  REQUIRE(ActionRegistry::Get().Bind(*persistent));

  State().failResult = true;
  ActionRegistry::Get().Dispatch({MakeRequest("12", "pick")});
  State().failResult = false;
  ActionRegistry::Get().Reset();
  ActionRegistry::Get().Dispatch({});

  CHECK(State().Contains("result:12:false:Game state changed"));
  CHECK_EQ(State().Count("execute"), size_t{0});
}

TEST("drop pending results: nothing owed to a dead session is sent later") {
  Fake::ResetWorld();
  auto persistent = LoggingAction("pick");
  REQUIRE(ActionRegistry::Get().Bind(*persistent));

  State().failResult = true;
  ActionRegistry::Get().Dispatch({MakeRequest("13", "pick")});
  State().failResult = false;
  ActionRegistry::Get().DropPendingResults();
  ActionRegistry::Get().Dispatch({});

  CHECK(!State().Contains("result:"));
  CHECK(!ActionRegistry::Get().HasPendingResult("pick"));
  ActionRegistry::Get().Unbind(*persistent);
}

TEST("dispatch: requests without an id are never answered or executed") {
  Fake::ResetWorld();
  auto persistent = LoggingAction("pick");
  REQUIRE(ActionRegistry::Get().Bind(*persistent));
  ActionRegistry::Get().Dispatch({MakeRequest("", "pick")});
  CHECK(!State().Contains("result:"));
  CHECK_EQ(State().Count("execute"), size_t{0});
  ActionRegistry::Get().Unbind(*persistent);
}

TEST("registry: duplicate names do not bind and unbind only removes the owner") {
  Fake::ResetWorld();
  auto first = LoggingAction("pick");
  auto second = LoggingAction("pick");
  CHECK(ActionRegistry::Get().Bind(*first));
  CHECK(!ActionRegistry::Get().Bind(*second));
  ActionRegistry::Get().Unbind(*second); // not the owner: must not remove first
  ActionRegistry::Get().Dispatch({MakeRequest("14", "pick")});
  CHECK(State().Contains("result:14:true:"));
  ActionRegistry::Get().Unbind(*first);
}

TEST("dispatch: an empty id leaves the window open and runs nothing") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(LoggingAction("pick"));
  REQUIRE(window.Register());

  ActionRegistry::Get().Dispatch({MakeRequest("", "pick")});

  CHECK(window.GetState() == ActionWindow::State::Registered);
  CHECK(!State().Contains("validate"));
  CHECK(!State().Contains("unregister:"));
  CHECK(!State().Contains("result:"));

  ActionRegistry::Get().Dispatch({MakeRequest("15", "pick")});
  CHECK(State().Contains("result:15:true:"));
}

TEST("retry: executions run after the whole queue was processed, so a callback may queue more results") {
  Fake::ResetWorld();
  ActionWindow first;
  first.Add(std::make_unique<Action>("first", [](const Request &) {
    return PreparedAction::Success([] {
      Log("execute:first");
      // Queues a new pending result (the send fails) while the retry loop is still running.
      State().failResult = true;
      ActionRegistry::Get().Dispatch({MakeRequest("extra", "nobody")});
    });
  }));
  ActionWindow second;
  second.Add(LoggingAction("second"));
  REQUIRE(first.Register());
  REQUIRE(second.Register());

  State().failResult = true;
  ActionRegistry::Get().Dispatch({MakeRequest("20", "first"), MakeRequest("21", "second")});
  CHECK(ActionRegistry::Get().HasPendingResult("first"));
  CHECK(ActionRegistry::Get().HasPendingResult("second"));
  State().failResult = false;

  ActionRegistry::Get().Dispatch({});

  CHECK_EQ(State().Count("result:20:true:"), size_t{1});
  CHECK_EQ(State().Count("result:21:true:"), size_t{1});
  CHECK_EQ(State().Count("execute:first"), size_t{1});
  CHECK_EQ(State().Count("execute"), size_t{1});
  ActionRegistry::Get().DropPendingResults();
}
