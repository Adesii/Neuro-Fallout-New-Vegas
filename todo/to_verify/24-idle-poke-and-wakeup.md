# 24 - Idle poke and post-action wake-up

Status: to verify
Area: Context / scheduling
Source: `docs/code-review.md` A1, A7.2 (greeting)

## Problem
Walker `Started`/`Completed` messages are silent (`GameplayHandler.cpp:496`, `silent = type != Failed`), walk actions
send their `action/result` before the walk even starts, and nothing nudges Neuro when she does nothing. After
"Reached X." she can sit idle forever. Skyrim solves this with a generic poke (`Socket.cpp:1177-1330`: random 6-10 s of
idleness, plus a 1 s delayed poke after an action completes; the text carries no state, suggestions were deliberately
commented out at `Socket.cpp:1229,1292`).

## Decisions (designer, 2026-10-21)
- Skyrim-style **generic non-silent nudge**: no state in the text, short phrases picked from a small set, written in
  **Fallout-flavoured wording** (e.g. "The wasteland waits. What do you do next?"), not Skyrim's bracketed lines. Draft
  ~6 phrases and show them to the designer before coding.
- Fires after ~6-10 s (randomised) of true idleness **and** ~1 s after an action finishes.
- **Repeats lengthen the interval** (grow towards ~30-60 s) and reset as soon as Neuro acts. Skyrim has an
  `afk_threshold` that behaves the same way.
- Greeting: silent, sent on connect, after every load / new game, and again every ~10 min (Skyrim `Socket.cpp:468,1203`).
  Short: game, her role, the discovery actions (`query_nearby`, `query_quests`, ...), plus the current location once
  task 28 exists.
- This **overrides** the "never during idle gameplay" line in todo 06; 06 is about busy/blocked moments only.

## Work
1. Engine-free `src/Policy/IdlePoker.*` (or beside `CombatLogic`): inputs are elapsed time, "busy" flags, "an action
   just completed", a seeded RNG; outputs are "poke now" and which phrase. Host tests for jitter bounds, backoff growth,
   reset on action, suppression, and the post-action delay.
2. Suppression inputs (from Skyrim, plus FNV equivalents): Walker active, combat engagement or `Combat::Engagement`
   active, any action window/force open (`ActionWindow` registered or a pending result), Pip-Boy/VATS executing, menu
   blocking gameplay, loading, disconnected.
3. Hook into `NeuroSDK::MainLoop` after the handlers ran; one owner for the timer so no second scheduler appears
   beside `NeuroSDK.cpp` (rule in `.claude/skills/skyrim-feature-port`).
4. Write `docs/design/idle-poke.md` before coding (skill `skyrim-feature-port`, step 5).
5. Greeting sender: reuse the same module; trigger from `NVSEMessagingInterface` load messages already handled in
   `plugin.cpp` (`PostLoadGame`, `NewGame`) and from a 10 min timer.

## Existing precedent
- Policy lives in engine-free modules with host tests: CLAUDE.md "Scheduling and policy logic", `src/Combat/CombatLogic.*`,
  `src/Movement/StanceLogic.*`.
- The one-force rule (`src/Actions/ActionWindow.cpp`) means a poke must never be sent while a force is outstanding.

## Open questions (ask the designer before coding)
- Final phrase list (Fallout-flavoured; designer to approve the draft).
- (Answered) The poke must **not** fire during long dialogue/menu waits, or in general while any force / action window is
  active.

## Result (2026-10-24)
Built (design note: `docs/design/idle-poke.md`):
- `src/Context/IdlePoker.*`: engine-free poke scheduler (6-10 s jitter, x1.7 backoff to a 30-60 s cap, 1 s post-action
  poke from the falling edge of "busy" when Neuro acted, 3 s minimum gap, reset on action, prompted-by-context counts as
  activity) plus `GreetingTimer` (10 min). 13 host tests in `tests/IdlePokerTests.cpp`.
- `src/Context/Greeting.*` builds the silent greeting. Owed on connect, load, new game and every 10 minutes; after a
  reconnect the status message leads with it instead.
- `NeuroSDK::ProcessIdlePoke` is the only scheduler owner; busy covers menus, loading, no game, death, Walker, combat
  engagement/context, Pip-Boy execution, an outstanding force (`ActionWindow::ForceOutstanding`) and pending results
  (`ActionRegistry::HasPendingResults`).
- The six phrases are a **draft**: the designer asked to approve the list before coding; the table is in
  `IdlePoker.cpp` and the same list is in the design note. Replace them there if they should change.

Unverified: everything in game (timing, that menus/loading block the poke, how the interval feels on stream). Compile and
host tests only.

Manual in-game checks: see `docs/design/idle-poke.md` (walk and wait 1 s, idle 30 s, Pip-Boy while idle, fight, load,
reconnect, 10 minute greeting).

## Done when
Walk completion and idle gameplay both lead to a nudge within the agreed window, repeated nudges back off, nothing is
sent during combat/menus/forces, timer logic is host-tested, and the manual checklist (walk, idle 30 s, open Pip-Boy
while idle) is given.
