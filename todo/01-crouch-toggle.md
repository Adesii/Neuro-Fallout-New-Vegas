# 01 - crouch_toggle action

Status: done (code complete, needs in-game verification)

## Problem
Neuro cannot sneak. Stealth needs crouching, and there is no action for it.

## Goal
A persistent gameplay action `crouch_toggle` that toggles the player between standing and crouching/sneaking, and
tells Neuro the resulting state.

## Notes
- Likely a tap of the Sneak control through `src/utils/InputUtils` (`ResolveControl`, `PulseQueue`), like combat's
  ready-weapon tap. Check how Walker/Combat already inject controls before adding anything new.
- Read the real sneak state back from the engine (do not assume the tap worked); the action result should say
  "now crouching" / "now standing" via context, since success results are empty by convention.
- Decide availability: gameplay only, not during menus/dialogue/VATS; blocked while Walker owns a conflicting
  control? Check that crouching does not break Walker pathing speed or the Combat engagement.
- Consider a state query: should `query_nearby` or the combat start message mention sneak/detection state?
  (Out of scope unless trivial.)

## Done when
Action registered in `src/NeuroSDK.cpp` set, validated, state verified in game; docs updated if it adds rules.

## Result
- `crouch_toggle` (`src/Actions/Gameplay/CrouchToggleAction.*`, `src/StanceHandler.*`, engine-free
  `src/Movement/StanceLogic.*` with `tests/StanceTests.cpp`). Gameplay-only; taps the bound Crouch control for 0.12 s and
  reports the state read back from the engine ("now crouching" / "now standing"), or "no effect" after 1.5 s.
- Sneak state is `PlayerCharacter::GetMovementFlags()` with bit `0x400` set and swimming `0x800` clear (JIP-LN-NVSE
  `Actor::IsSneaking`, itr-nvse `CrouchCommands`); the vendored JG comment "bit 9" disagrees and is not used.
- Unverified: that the flag reads correctly in game, and that the Crouch binding toggles rather than needs holding.
- Manual checks: crouch then stand while watching the HUD sneak indicator; confirm the context message matches; try it
  while swimming/in a menu (rejected or "no effect"); confirm no key stays held.
