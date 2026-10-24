#include "Actions/ActionRegistry.hpp"
#include "Actions/ActionWindow.hpp"
#include "Actions/PersistentActionSet.hpp"
#include "FakeNeuro.hpp"
#include "TestFramework.hpp"
#include "utils/ScopedState.hpp"

using namespace Actions;
using Fake::Action;
using Fake::State;

namespace {

Request MakeRequest(std::string id, std::string name) { return {std::move(id), std::move(name), "{}"}; }

void ForceOn(ActionWindow &window) { window.SetForce("choose", "state", NeuroSDK::ActionPriority::Medium); }

bool IsBound(const std::string &name) {
  const auto before = State().log.size();
  ActionRegistry::Get().Dispatch({MakeRequest("probe", name)});
  const bool unknown = State().log.size() > before && State().log.back().find(":false:Unknown") != std::string::npos;
  return !unknown;
}

} // namespace

TEST("action names: lowercase letters and inner underscores only") {
  CHECK(IsValidActionName("select_dialog"));
  CHECK(IsValidActionName("a"));
  CHECK(!IsValidActionName(""));
  CHECK(!IsValidActionName("_leading"));
  CHECK(!IsValidActionName("trailing_"));
  CHECK(!IsValidActionName("Upper"));
  CHECK(!IsValidActionName("with space"));
  CHECK(!IsValidActionName("digit1"));
  CHECK(!IsValidActionName("dash-ed"));
}

TEST("window: registers then forces, in that order") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("one")).Add(std::make_unique<Action>("two"));
  ForceOn(window);
  REQUIRE(window.Register());
  const std::vector<std::string> expected = {"register:one,two", "force:one,two"};
  CHECK(State().log == expected);
  CHECK(window.GetState() == ActionWindow::State::Forced);
  CHECK(window.End());
  CHECK(window.GetState() == ActionWindow::State::Ended);
}

TEST("window: only one window may own a force") {
  Fake::ResetWorld();
  ActionWindow first;
  first.Add(std::make_unique<Action>("one"));
  ForceOn(first);
  REQUIRE(first.Register());

  ActionWindow second;
  second.Add(std::make_unique<Action>("two"));
  ForceOn(second);
  CHECK(!second.Register());
  CHECK(second.GetState() == ActionWindow::State::Faulted);
  CHECK(!State().Contains("register:two"));

  REQUIRE(first.End());
  ActionWindow third;
  third.Add(std::make_unique<Action>("three"));
  ForceOn(third);
  CHECK(third.Register());
  third.End();
}

TEST("window: unforced windows never contend for the force guard") {
  Fake::ResetWorld();
  ActionWindow forced;
  forced.Add(std::make_unique<Action>("one"));
  ForceOn(forced);
  REQUIRE(forced.Register());
  ActionWindow plain;
  plain.Add(std::make_unique<Action>("two"));
  CHECK(plain.Register());
  plain.End();
  forced.End();
}

TEST("window: destroying a forced window releases the force guard") {
  Fake::ResetWorld();
  auto dropped = std::make_unique<ActionWindow>();
  dropped->Add(std::make_unique<Action>("one"));
  dropped->SetForce("choose", "state", NeuroSDK::ActionPriority::Medium);
  REQUIRE(dropped->Register());
  dropped.reset();

  // Occupy the freed address so a stale guard cannot match a new window by coincidence.
  auto filler = std::make_unique<ActionWindow>();
  ActionWindow next;
  next.Add(std::make_unique<Action>("two"));
  ForceOn(next);
  CHECK(next.Register());
  next.End();
}

TEST("window: destroying a registered window unbinds its actions") {
  Fake::ResetWorld();
  auto dropped = std::make_unique<ActionWindow>();
  dropped->Add(std::make_unique<Action>("one"));
  REQUIRE(dropped->Register());
  REQUIRE(IsBound("one"));
  dropped.reset(); // without End()/Release(): the registry must not keep dangling pointers

  CHECK(!IsBound("one"));
  ActionWindow next;
  next.Add(std::make_unique<Action>("one"));
  CHECK(next.Register());
  next.End();
}

TEST("window: destroying a forced or closing window unbinds its actions too") {
  Fake::ResetWorld();
  {
    ActionWindow forced;
    forced.Add(std::make_unique<Action>("one"));
    ForceOn(forced);
    REQUIRE(forced.Register());
  }
  CHECK(!IsBound("one"));

  {
    ActionWindow closing;
    closing.Add(std::make_unique<Action>("two"));
    REQUIRE(closing.Register());
    State().failUnregister = true;
    CHECK(!closing.End());
    CHECK(closing.GetState() == ActionWindow::State::Closing);
  }
  State().failUnregister = false;
  CHECK(!IsBound("two"));
}

TEST("window: release unregisters on the server when connected") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("one"));
  ForceOn(window);
  REQUIRE(window.Register());
  window.Release();
  CHECK(State().Contains("unregister:one"));
  CHECK(window.GetState() == ActionWindow::State::Ended);
  CHECK(!IsBound("one"));
}

TEST("window: release falls back to a local abandon when the server cannot be told") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("one"));
  ForceOn(window);
  REQUIRE(window.Register());
  State().failUnregister = true;
  window.Release();
  CHECK(window.GetState() == ActionWindow::State::Ended);
  CHECK(!IsBound("one"));

  State().failUnregister = false;
  ActionWindow next;
  next.Add(std::make_unique<Action>("two"));
  ForceOn(next);
  CHECK(next.Register()); // the force guard was released too
  next.End();
}

TEST("window: invalid or duplicate names fault before anything is sent") {
  Fake::ResetWorld();
  ActionWindow invalid;
  invalid.Add(std::make_unique<Action>("Bad Name"));
  CHECK(!invalid.Register());
  CHECK(invalid.GetState() == ActionWindow::State::Faulted);

  ActionWindow duplicate;
  duplicate.Add(std::make_unique<Action>("same")).Add(std::make_unique<Action>("same"));
  CHECK(!duplicate.Register());
  CHECK(State().log.empty());
}

TEST("window: empty windows and late additions are refused") {
  Fake::ResetWorld();
  ActionWindow empty;
  CHECK(!empty.Register());

  ActionWindow window;
  window.Add(std::make_unique<Action>("one"));
  REQUIRE(window.Register());
  window.Add(std::make_unique<Action>("late"));
  CHECK_EQ(window.GetActions().size(), size_t{1});
  window.End();
}

TEST("window: a name already bound elsewhere faults without touching the server") {
  Fake::ResetWorld();
  ActionWindow first;
  first.Add(std::make_unique<Action>("one"));
  REQUIRE(first.Register());
  const auto sent = State().log.size();

  ActionWindow clash;
  clash.Add(std::make_unique<Action>("one"));
  CHECK(!clash.Register());
  CHECK_EQ(State().log.size(), sent);
  first.End();
}

TEST("window: failed server registration rolls back the local binding and the force") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("one"));
  ForceOn(window);
  State().failRegister = true;
  CHECK(!window.Register());
  CHECK(window.GetState() == ActionWindow::State::Faulted);
  State().failRegister = false;

  CHECK(!IsBound("one"));
  ActionWindow retry;
  retry.Add(std::make_unique<Action>("one"));
  ForceOn(retry);
  CHECK(retry.Register());
  retry.End();
}

TEST("window: failed force closes the window it just opened") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("one"));
  ForceOn(window);
  State().failForce = true;
  CHECK(!window.Register());
  CHECK(window.GetState() == ActionWindow::State::Ended);
  CHECK(State().Contains("unregister:one"));
}

TEST("window: ending a faulted window sends nothing") {
  Fake::ResetWorld();
  ActionWindow window;
  window.Add(std::make_unique<Action>("Bad"));
  REQUIRE(!window.Register());
  CHECK(window.End());
  CHECK(State().log.empty());
}

TEST("persistent set: registers once and unregisters once") {
  Fake::ResetWorld();
  PersistentActionSet set;
  set.Add(std::make_unique<Action>("one")).Add(std::make_unique<Action>("two"));
  CHECK(set.Register());
  CHECK(set.Register());
  CHECK_EQ(State().Count("register:one,two"), size_t{1});
  CHECK(IsBound("one"));
  CHECK(set.Unregister());
  CHECK(set.Unregister());
  CHECK_EQ(State().Count("unregister:one,two"), size_t{1});
  CHECK(!IsBound("one"));
}

TEST("persistent set: failed unregister is unbound at once and retried before registering again") {
  Fake::ResetWorld();
  PersistentActionSet set;
  set.Add(std::make_unique<Action>("one"));
  REQUIRE(set.Register());

  State().failUnregister = true;
  CHECK(!set.Unregister());
  CHECK(!IsBound("one"));

  State().failUnregister = false;
  CHECK(set.Register());
  CHECK_EQ(State().Count("unregister:one"), size_t{1});
  CHECK_EQ(State().Count("register:one"), size_t{2});
  CHECK(IsBound("one"));
  set.Unregister();
}

TEST("persistent set: failed server registration leaves nothing bound") {
  Fake::ResetWorld();
  const auto start = ScopedState::Clock::now();
  PersistentActionSet set;
  set.Add(std::make_unique<Action>("one")).Add(std::make_unique<Action>("two"));
  State().failRegister = true;
  CHECK(!set.Register(start));
  State().failRegister = false;
  CHECK(!IsBound("one"));
  CHECK(!IsBound("two"));
  CHECK(set.Register(start + PersistentActionSet::kRetryDelay));
  set.Unregister();
}

TEST("persistent set: a failed registration is not retried until the backoff has passed") {
  Fake::ResetWorld();
  const auto start = ScopedState::Clock::now();
  PersistentActionSet set;
  set.Add(std::make_unique<Action>("one"));
  State().failRegister = true;
  CHECK(!set.Register(start));
  State().failRegister = false;

  // Every frame inside the delay is a cheap no-op: nothing is built, bound or sent.
  CHECK(!set.Register(start + std::chrono::milliseconds(500)));
  CHECK(!set.Register(start + PersistentActionSet::kRetryDelay - std::chrono::milliseconds(1)));
  CHECK(!State().Contains("register:"));
  CHECK(!IsBound("one"));

  CHECK(set.Register(start + PersistentActionSet::kRetryDelay));
  CHECK_EQ(State().Count("register:one"), size_t{1});
  set.Unregister();
}

TEST("persistent set: resetting the registry clears the backoff so the set is restored at once") {
  Fake::ResetWorld();
  const auto start = ScopedState::Clock::now();
  PersistentActionSet set;
  set.Add(std::make_unique<Action>("one"));
  State().failRegister = true;
  CHECK(!set.Register(start));
  State().failRegister = false;
  CHECK(!set.Register(start));

  ActionRegistry::Get().Reset(); // reconnect or load
  CHECK(set.Register(start));
  set.Unregister();
}

TEST("persistent set: invalid names are rejected") {
  Fake::ResetWorld();
  PersistentActionSet set;
  set.Add(std::make_unique<Action>("Bad"));
  CHECK(!set.Register());
  CHECK(State().log.empty());
}

TEST("transport: the fake enforces the real preconditions") {
  Fake::ResetWorld();
  CHECK(!NeuroSDK::RegisterActions({}));
  Actions::Definition array;
  array.name = "bad";
  array.schema = Json::JsonSchema::Array();
  CHECK(!NeuroSDK::RegisterActions({array}));
  CHECK(!NeuroSDK::SendActionResult("", true, ""));
  CHECK(!NeuroSDK::ForceActions({}, "query", "", NeuroSDK::ActionPriority::Low));
  CHECK(!NeuroSDK::ForceActions({"one"}, "", "", NeuroSDK::ActionPriority::Low));
  CHECK(!NeuroSDK::ForceActions({""}, "query", "", NeuroSDK::ActionPriority::Low));
  CHECK(NeuroSDK::UnregisterActions({}));
  CHECK(!NeuroSDK::SendContext("", false));
  CHECK(!NeuroSDK::SendContext("   \n", true));
  CHECK(!NeuroSDK::SendContext("[]", true));
  CHECK(!NeuroSDK::SendContext(nullptr, true));
  CHECK(State().log.empty()); // nothing reached the wire
}

TEST("transport: the fake sends nothing while disconnected, like the real one") {
  Fake::ResetWorld();
  State().connected = false;
  Actions::Definition definition;
  definition.name = "ok";
  CHECK(!NeuroSDK::SendContext("hello", false));
  CHECK(!NeuroSDK::RegisterActions({definition}));
  CHECK(!NeuroSDK::UnregisterActions({"ok"}));
  CHECK(!NeuroSDK::ForceActions({"ok"}, "query", "", NeuroSDK::ActionPriority::Low));
  CHECK(!NeuroSDK::SendActionResult("id", true, ""));
  CHECK(NeuroSDK::UnregisterActions({})); // nothing to send is not a failure
  CHECK(State().log.empty());
  State().connected = true;
  CHECK(NeuroSDK::SendContext("hello", false));
}
