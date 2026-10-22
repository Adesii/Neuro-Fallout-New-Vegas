# 30 - Context reliability: disconnects, dedupe, subtitles

Status: open
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
