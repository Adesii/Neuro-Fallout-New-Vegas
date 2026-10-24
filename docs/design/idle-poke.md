# Idle poke, post-action wake-up and greeting (todo 24)

## Goal
After she finishes something, or does nothing for a while, Neuro gets a short non-silent nudge so she keeps playing.
Walker `Started`/`Completed` messages are silent and an action's `action/result` is sent before the walk even starts, so
without this she can sit idle after "Reached X." forever. Decisions are the designer's (todo 24, 2026-10-21).

## Skyrim behaviour (Neuro-Skyrim-Integration, read-only; idea source only)
- `NeuroSocket::Tick` (`src/Socket.cpp`, ~1177-1330) counts `time_no_commands`. When it passes a random 6-10 s
  threshold and the walker and the menus have also been inactive for that long, it sends a phrase from a list of six
  bracketed lines (`[Choose next action to do]`, `[Do something]`, `...`) and bumps `afk_threshold`.
- A delayed poke fires one second after an action finished, but only when no menu is open, something is registered and the
  walker is not active ("attempt to reduce adhd"). Opening a menu cancels it.
- Any message from Neuro zeroes `time_no_commands` and `afk_threshold`.
- The greeting goes out on connect and every 600 s (`Socket.cpp:468,1195`).
- Why (inferred): the poke carries no state because state belongs in queries; suggestions were commented out because they
  steered her into a rut. Waiting for a quiet game avoids interrupting a fight or a menu choice.
- Does not transfer: the Skyrim script layer, `RaceProcessor` and `have_force_only_menu_open`.

## Contract
`context` with `silent: false` prompts the model to respond (neuro-sdk `API/SPECIFICATION.md`); a silent one only adds
information. One action force at a time (`ActionWindow.cpp`): a poke is never sent while a force is outstanding.

## Design
- `src/Context/IdlePoker.*` (engine-free, `tests/IdlePokerTests.cpp`): a state machine fed once a frame with
  `{dt, busy, neuroActed}`; it returns "poke now" and a phrase index.
  - Idle time only counts while not busy; any busy frame resets it.
  - The first poke comes after a random 6-10 s of idleness. Each repeat multiplies the interval by 1.7, capped at a value
    drawn from 30-60 s, so repeats back off towards 30-60 s. `neuroActed` resets the backoff.
  - "An action just finished" is the falling edge of `busy`, one second later, and only if Neuro acted since the busy
    period began (so loading a save or a cutscene ending does not count). It is cancelled if the game turns busy again.
  - Never two pokes closer than 3 s, whatever the trigger; never the same phrase twice in a row.
  - `NotifyPrompted()`: a non-silent context message counts as activity (it already prompted her), so a progress
    reminder, combat message or quest update never stacks with a poke.
- Phrases (Fallout-flavoured, no state, no ids, no suggestions): "The wasteland waits. What do you do next?", "Another quiet
  moment in the Mojave. What's your next move?", "The road ahead is yours to choose.", "Nothing is stopping you. Pick your
  next action.", "The desert doesn't wait forever. Decide what to do next.", "Still standing out here, Courier? Do something."
  A draft: the designer asked to see the list; it is a table in `IdlePoker.cpp` and is trivial to change.
- One owner: `NeuroSDK::MainLoop` calls `ProcessIdlePoke` at the end of the frame, after every handler ran. `busy` is
  true when a menu blocks gameplay (this covers dialogue, VATS, popups, loading, unknown menus), no game is loaded, the
  player is dead, Walker is active, a combat engagement or the combat context is active, the Pip-Boy is executing, an
  `ActionWindow` owns the force (`ActionWindow::ForceOutstanding`) or an action result is pending
  (`ActionRegistry::HasPendingResults`). Disconnected sessions never reach it.
- `neuroActed` is "the action inbox was not empty this frame".
- Greeting: `Context::BuildGreeting` (`src/Context/Greeting.*`): the game, her role, the discovery actions. Silent. Owed on
  connect (first connection included), after `PostLoadGame` / `NewGame` (`plugin.cpp` calls `RequestGreeting`) and every
  10 minutes (`GreetingTimer`). It waits until a game is loaded. After a reconnect the status message
  (`GameplayHandler::BuildFreshStatus`) leads with it and the separate greeting is skipped, so the two never arrive
  back to back. The location line joins it with todo 28.
- `ResetAutomation` (preload, new game, exit to menu, disconnect) resets the poker.

## Unverified assumptions
- That "menu blocks gameplay" really is true during every loading screen and cutscene; if it is not, a poke could land
  there. After a busy period the poker pokes after a second only if Neuro acted, otherwise after 6-10 s of idleness.
- Interval numbers (6-10 s, x1.7, cap 30-60 s, 1 s post-action) are the designer's/Skyrim's and untested on stream.
- Whether a non-silent poke interrupts Neuro mid-speech in a way that feels bad; tune the cap if so.

## Manual in-game checklist
1. Connect with a game loaded and idle: one silent greeting appears in the log (`Sent the greeting to Neuro`); no poke
   for 6-10 s, then one poke, then the next after roughly 10-17 s, then 17-29 s, then every 30-60 s.
2. `move_to_object` to something close: about one second after "Reached" a poke arrives; none while walking.
3. Open the Pip-Boy or talk to an NPC and wait 60 s: no poke during the menu or dialogue; after closing it, a poke only after
   Neuro's action (menu actions count) or the normal idle time.
4. Fight something: no poke during the engagement or the combat update batches.
5. Load a save: greeting again (silent), no poke from before the load. Reconnect the server: one status message that
   includes the welcome text, not two messages.
6. Idle for 10+ minutes: the greeting repeats silently.
