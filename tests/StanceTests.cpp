#include "Movement/StanceLogic.hpp"
#include "TestFramework.hpp"

using namespace Stance;

TEST("stance: only the sneak bit counts, and swimming masks it") {
  CHECK(IsSneakingFlags(0x400));
  CHECK(IsSneakingFlags(0x400 | 0x1 | 0x100)); // moving while sneaking
  CHECK(!IsSneakingFlags(0x0));
  CHECK(!IsSneakingFlags(0x200));         // running
  CHECK(!IsSneakingFlags(0x400 | 0x800)); // swimming masks the sneak bit
}

TEST("stance: the verifier reports a crouch once the state flips") {
  CrouchVerifier verifier;
  CHECK(!verifier.IsPending());
  verifier.Begin(false);
  CHECK(verifier.IsPending());
  CHECK(verifier.Update(0.1f, false) == Outcome::Pending);
  CHECK(verifier.Update(0.1f, true) == Outcome::Changed);
  CHECK(!verifier.IsPending());
  CHECK(verifier.Update(0.1f, true) == Outcome::Pending); // resolved once
}

TEST("stance: the verifier reports standing up") {
  CrouchVerifier verifier;
  verifier.Begin(true);
  CHECK(verifier.Update(0.0f, false) == Outcome::Changed);
}

TEST("stance: the verifier reports no effect after the timeout") {
  CrouchVerifier verifier;
  verifier.Begin(false);
  Outcome outcome = Outcome::Pending;
  for (int i = 0; i < 100 && outcome == Outcome::Pending; ++i)
    outcome = verifier.Update(0.05f, false);
  CHECK(outcome == Outcome::NoEffect);
  CHECK(!verifier.IsPending());
}

TEST("stance: cancelling the verifier clears the pending toggle") {
  CrouchVerifier verifier;
  verifier.Begin(false);
  verifier.Cancel();
  CHECK(!verifier.IsPending());
  CHECK(verifier.Update(5.0f, false) == Outcome::Pending);
}

TEST("stance: descriptions name the resulting state") {
  CHECK(DescribeCrouched(true).find("crouching") != std::string::npos);
  CHECK(DescribeCrouched(false).find("standing") != std::string::npos);
  CHECK(DescribeNoEffect(true).find("still crouching") != std::string::npos);
}
