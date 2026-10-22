# 30 - Context reliability: disconnects, dedupe, subtitles

Status: to verify
Area: `NeuroSDK.cpp`, `SubtitleHandler`, `MenuHandler`
Source: `docs/code-review.md` A7.4, A7.6, B8, B9, S2

## Problem
- `NeuroSDK::SendContext` returns false and logs a warning on **every** call while disconnected
  (`NeuroSDK.cpp:208-212`); most callers ignore the result, so quest completions and "combat ended" notes are lost.
- No filter in front of `SendContext`: Skyrim's `send_random_context` drops empty/junk text and rate-limits repeats
  (`main.cpp:1104-1130`), and the delayed-message path drops consecutive duplicates (`Socket.cpp:1190`).
- `SubtitleHandler` is never reset on disconnect/load/new game (`SubtitleHandler.cpp`, `MenuHandler::Reset`):
  stale subtitles can flush into the new session and the dedupe key (`startTime`) can swallow a repeated bark.
- Subtitle lines carry no speaker, so Neuro cannot tell who talks (Skyrim: `Name[Emotion] says "text"`,
  `main.cpp:1032-1096`).
- `PollMessages` destroys only Action messages (`NeuroSDK.cpp`, pinned library frees nothing else today).

## Decisions (designer, 2026-10-21)
- On disconnect: **drop** non-silent context sent in between (it would be stale). After reconnect send a **fresh
  status**: greeting (todo 24) + location (todo 28) + active quest/health summary. **Rate-limit** the
  "not connected" warning.
- Subtitle speaker: **research only, no guessing**: add the speaker only when engine data provides it directly.

## Work
1. `SendContext`: log the disconnected warning once per outage; callers that clear state on success
   (`FlushNotes`, subtitles) must keep checking the bool.
2. Reconnect status: single function that builds the "fresh status" message; reuse existing builders
   (`BuildPlayerStatus`, quest summary, location line).
3. Optional engine-free filter before `SendContext`: collapse consecutive identical messages within N seconds, drop
   empty text (already done). Host-tested.
4. `SubtitleHandler::Reset()` called from `MenuHandler::Reset()`; also change the dedupe key so a repeated line after a
   load is not swallowed.
5. Research step for the speaker: inspect the subtitle struct (`HUDMainMenu::subtitlesArr`, `src/defs/`), the HUD
   subtitle tile children and the subtitle XML in the private extracts; record the result in
   `docs/reference-research.md` terms. Implement only if a field/tile exists. Do not infer the speaker from proximity.
6. `PollMessages`: destroy every polled message or comment the pinned-library assumption.

## Existing precedent
- `ScopedState::Backoff` / `ScopedState` for time-based retry policy; `SubtitleHandler` already uses an explicit retry
  delay on failed sends.

## Answered (designer, 2026-10-21)
- The fresh status after a reconnect is **non-silent** so she re-orients.

## Done when
A mid-fight disconnect/reconnect produces one warning, no stale notes, and a single fresh status; loading a save clears
subtitle state; research result for the speaker is written down.

## Result (2026-10-22)
Built and committed. Host tests: 137 pass (new: `ContextGate`, `SubtitleTracker`); DLL builds. Nothing was run in game.
The design note is `docs/design/sdk-context-reliability.md` (written after the Skyrim study; the designer's decisions
in this file served as its approval).

- `SendContext`: one "not connected" warning per outage; non-silent context sent while disconnected is dropped (it
  returns false, so `FlushNotes` and subtitle batches keep their state and nothing is queued for replay).
- New engine-free `Context::ContextGate` in front of the transport: blank text and `[]` are rejected, and the same text
  with the same silence within 3 s of the last sent message is dropped (reported as delivered). A new connection
  re-arms the warning and forgets the last message. **Trade-off:** an identical query result requested twice within
  3 s is dropped; see the design note's open question.
- Reconnect status: `GameplayHandler::BuildFreshStatus(reconnect)` (existing `BuildPlayerStatus` plus a short list of
  active objectives, names only so it never rebuilds the `query_quests` ids) is sent non-silent once per connection,
  as soon as a game is loaded. **Greeting (todo 24) and location (todo 28) are not implemented**: there is a marked
  slot in the builder and those two tasks stay open.
- `CombatHandler::FlushNotes` clears its notes and starts the cooldown only after the send succeeded.
- Subtitles: `SubtitleTracker` reports an entry only while it is new in the HUD array and forgets entries that left it,
  so a repeated line, or a restarted `startTime` after a load, is not swallowed; `SubtitleHandler::Reset()` (called from
  `MenuHandler::Reset`) clears the tracker and the unsent batch.
- Subtitle speaker: **researched, not implemented.** `HUDMainMenu::SubtitleData` has text, start time, display time,
  position and priority, no actor; the HUD XML tile carries only the string; JohnnyGuitarNVSE's
  `OnGeneralSubtitle` hook exposes a `target` that its own comment calls "usually the player" (the listener). Only a
  position-to-nearest-actor guess would produce a name, which the decision forbids. Details and the possible future
  route (a hook on the dialogue path) are in the design note.
- `PollMessages` destroys every polled message (shared with todo 33).

### Unverified
- That `neurosdk_context_connected` flips to false promptly after a server restart (the existing path relies on it).
- The 3 s duplicate window is a guess.

### Manual in-game checklist
See the design note; in short: stop the server mid-fight (one warning, no stale combat notes), restart it (one "Status
after reconnecting" message with health, weapon and objectives), start the server at the main menu and load a save (one
status after the load), load a save with subtitles on screen (no stale batch, a repeated bark is reported afterwards).
