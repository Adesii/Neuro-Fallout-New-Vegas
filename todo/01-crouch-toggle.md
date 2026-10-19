# 01 - crouch_toggle action

Status: open

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
