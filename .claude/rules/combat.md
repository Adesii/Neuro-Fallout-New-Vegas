---
paths:
  - "src/CombatHandler.*"
  - "src/Combat/**"
  - "src/Menus/VatsHandler.*"
  - "src/Actions/Combat/**"
  - "src/defs/VatsMenu.hpp"
  - "src/utils/AimUtils.*"
  - "src/utils/InputUtils.*"
  - "docs/combat.md"
---

# Combat and VATS scope

- `src/Combat/DamageLogic.*` (hit notes, death report; `tests/DamageTests.cpp`) is the engine-free half of damage and
  death reporting; `CombatHandler.cpp` feeds it the OnHit queue and engine reads. Notes are only cleared after a send.
- `src/Combat/CombatLogic.*` is engine-free (classification, the `Engagement` state machine, `CombatReporter`, argument
  parsing) and is covered by `tests/CombatTests.cpp`. Put new decision logic there and add a test; keep game memory
  reads in `src/CombatHandler.cpp`, which only feeds observations in and applies outputs.
- `attack` must stay callable during an engagement (retarget) and `stop_attack` must stay registered while menus are
  open. An engagement pauses across menus, VATS and the Pip-Boy and ends only for the reasons in `Combat::StopReason`;
  every ending sends a non-silent context message saying why.
- Only explicitly friendly actors (faction relation Ally/Friend or teammate, unless fighting the player) are excluded
  from enemies, attack and VATS. Never exclude neutrals.
- Held controls are owned: `InputUtils::SetControlHeld(owner, ...)` and `PulseQueue(owner)` use `InputCore::Owner` bits, so releasing never cancels another owner's press, and `NeuroSDK::ResetAutomation` ends with `DIHookControl::ClearAllScripted()` as a backstop.
- Every exit path must release held inputs (`ReleaseInputs`, `PulseQueue::ReleaseAll`): pause, reset, disconnect,
  death, load, retarget. The attack control is usually a mouse button; resolve it with `InputUtils::ResolveControl`.
- Combat owns aim while engaged (`Walker::SetLookSuppressed`); Walker keeps moving. Do not let Walker and combat both
  write yaw/pitch. Combat's own walk toward its target (melee reach, or a view of it when `attack` was given
  `approach`) uses `Walker::Owner::Combat` and must never replace another owner's command: `attack` stops whatever
  walk was running when it starts and ends the quest route (todo 50), and a walk Neuro starts afterwards wins over the
  approach. A failed approach walk ends the engagement (`StopReason::Unreachable`); do not restart it.
- Shots at an actor aim at its torso node from the game's body part data (`AimUtils::ActorTorsoPoint`, todo 56), then
  its mesh bound centre. There is no guessed height for actors; do not add one back as a fallback.
- VATS blocking must be corroborated by the menu being on top; the mode global alone must not lock the game (see the
  playback watchdog). VATS input mappings are unverified: keep each step state-verified with a bounded timeout.
- Read `docs/combat.md` for evidence, unverified assumptions and the manual in-game checklist before changing any of it.
