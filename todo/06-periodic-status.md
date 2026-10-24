# 06 - Periodic status reminders

Status: open

## Problem
While waiting for something to finish (long dialogue sequences, the Vit-O-Matic during character creation, other
cutscene-like moments) Neuro hears nothing and does not know what is going on.

## Scope note (2026-10-21)
The generic idle/post-action nudge is a separate system, see `24-idle-poke-and-wakeup.md` (designer decision: Skyrim-style
generic non-silent poke, which also fires during idle gameplay). This todo is only about *describing what is going on*
while the game is busy or blocked. Do not merge the two; make sure a status reminder counts as activity for the poke
timer so they never stack.

Update (2026-10-24): the poke from todo 24 is in (`Context::IdlePoker`). A non-silent status reminder only has to be sent through
`NeuroSDK::SendContext`: that already calls `IdlePoker::NotifyPrompted()`, so a reminder counts as activity and the two never
stack. The poker treats a busy game (menus, Walker, combat, forces) as "not idle" on its own.

## Goal
A status reminder sent every ~5-10 s while the game is busy or blocked, describing where she is and what she is doing
(current menu/phase, active Walker command, combat engagement, dialogue speaker, waiting for ...).

## Notes
- Follow the SDK practices: occasional, meaningful, no streaming of small changes. Reminders should repeat only when
  something is ongoing and state is not already known, and should be silent vs non-silent by design (ask the designer;
  default to silent so she is not interrupted).
- Central place: `MenuHandler::Process()` / `NeuroSDK::MainLoop()`; each handler can contribute a one-line status
  (`DescribeStatus()`-style) rather than the loop knowing every system.
- Keep the throttle and "what changed" logic engine-free so it can be host-tested (timer, dedupe, interval).
- Beware overlap with combat updates (batched >= 3 s) and subtitle context; avoid duplicating them.
- Examples to cover: dialogue playback, Vit-O-Matic / character creation screens, loading, VATS playback, a Walker
  command in progress, an `attack` engagement, a menu waiting for her choice.

## Done when
Reminders arrive at the chosen interval during the examples above, never during plain idle gameplay (the poke in todo 24
owns that), and are tested on the host.
