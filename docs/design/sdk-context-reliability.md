# SDK context reliability (todo 30)

## Goal
Context sent around a disconnect is neither lost silently nor replayed stale; Neuro re-orients after a reconnect;
identical context is not repeated back to back; subtitle state follows the game session. Decisions are the designer's
(todo 30, 2026-10-21): drop non-silent context sent while disconnected, send one non-silent fresh status after
reconnecting, rate-limit the "not connected" warning, add a subtitle speaker only when the engine provides it.

## Skyrim behaviour (Neuro-Skyrim-Integration, read-only)
- `send_random_context` (`src/main.cpp`, ~1104-1130) is the single funnel for ad-hoc context. It returns early for empty
  text, `"[]"`, and a handful of known-noise strings; it rate-limits repeated "resisted" messages to one per 10 s. Why
  (inferred): everything funnels through one place so noise can be cut without touching every producer.
- Delayed messages drop a consecutive duplicate (`src/Socket.cpp`, ~1190). Why (inferred): the same batched line queued
  twice reads as a stutter.
- Speech: `send_speech_context` (`main.cpp`, ~1032-1096) formats `Name[Emotion] says "text"` from the speaker's
  `TESObjectREFR`, which Skyrim's script events hand to it directly.
- A greeting is re-sent every 600 s (`Socket.cpp` Tick) and on connect; this is todo 24, not part of this task.

## What transfers
- One funnel in front of the transport that drops empty text and collapses consecutive duplicates.
- Fresh orientation after (re)connecting.

## What does not transfer
- The speaker reference: Skyrim receives it from the event; FNV's subtitle array carries none (below).
- Skyrim's known-noise string list is game specific.

## Contract (neuro-sdk)
`context` has `message` and `silent`. Context sent before a disconnect survives on the server's side of the session, so
nothing sent earlier is replayed; messages sent while disconnected cannot be delivered at all (the library's
`neurosdk_context_send` fails). After reconnecting the game must send `startup` and re-register its actions
(`NeuroSDK::MainLoop` already does both).

## Design
- `src/Context/ContextGate.*` (engine-free, host-tested): `Check(text, silent, now)` -> Send / Empty / Duplicate. Empty
  is blank text or `[]`. A duplicate is the same text and silence as the last message that was sent, within 3 s. Why
  3 s: long enough to remove a stutter, short enough not to swallow a query result Neuro asked for again. It also holds
  the outage flag: `ShouldWarnOutage()` is true once per outage and `Connected()` re-arms it and forgets the last message.
- `NeuroSDK::SendContext` consults the gate. Disconnected: one warning per outage, returns false (the context is
  dropped; callers that clear state on success still see false). Duplicate: returns true (it was already delivered).
  Empty: unchanged (warn, false).
- Reconnect status: `GameplayHandler::BuildFreshStatus()` builds one message from existing builders
  (`CombatHandler::BuildPlayerStatus`, a short active-objective list) and returns nothing until a game is loaded.
  `NeuroSDK` marks it pending when a connection becomes ready *again* (the first connection of a session gets none: the
  decision was a status after a reconnect, and the greeting is todo 24) and sends it non-silent once the builder
  produces text; only a successful send clears the flag. Slots for the greeting (todo 24) and the location line (todo 28) are marked in
  the builder and are not implemented here, since those tasks are open.
- Combat notes: `FlushNotes` clears its list and starts the cooldown only after the send succeeded.
- Subtitles: `src/Menus/SubtitleTracker.*` (engine-free, host-tested). Each observation compares the HUD's current
  subtitle entries with the previous observation and reports only the new ones; entries that left the array are
  forgotten, so a line repeated later, or a `startTime` that restarted after a load, is never swallowed.
  `SubtitleHandler::Reset()` (called from `MenuHandler::Reset`) clears the tracker, the pending batch and the flush
  schedule.
- `PollMessages` destroys every polled message (shared with todo 33).

## Subtitle speaker: research result (no speaker is added)
Evidence, all from project and reference sources:
- `HUDMainMenu::SubtitleData` (`libs/nvse/JG/nvse/GameUI.h:915`, same layout in JIP `nvse/GameUI.h:685`) is `{char text[0x104];
  UINT32 startTime; float displayTime; NiPoint3 position; INT32 priority}`. There is no actor, form id or name.
- `menus/main/hud_main_menu.xml` (private extract) declares only the `Subtitles` rect (800x110) with
  `justify_center_text`; the tile carries the string and nothing about who spoke.
- JohnnyGuitarNVSE hooks `HUDMainMenu::AppendSubtitleData` (call at `0x7052B8`, from `Interface::ShowText`) and hands its
  script `(text, sound, position, target, instant)`. JG documents `target` as "Target Reference (Usually Player)": it is
  the listener, not the speaker. `position` is the source position only.
Conclusion: the engine data does not name the speaker. The only way to produce a name would be to match `position` to
the nearest actor, which the designer ruled out. Nothing was implemented. A future change would need a hook on the
dialogue/`SayTopic` path (`JIP GameObjects.h:10` has `SayTopic(..., TESObjectREFR *target, ...)`) and its own research.

## Unverified assumptions
- `neurosdk_context_connected` turns false promptly after a server restart (the existing disconnect path relies on it).
- Reconnect timing relative to the player being loaded: the status waits for a loaded player, not for a stable frame.
- 3 s is a guess at what repeats are noise.

## Manual in-game checklist
1. Mid-fight, stop the Neuro server: the log shows exactly one "not connected" warning, then none until reconnect.
2. Restart the server during the fight: Neuro receives one "Status after reconnecting" message with health, weapon and
   objectives, and no "Combat update" lines from the outage.
3. Start the game with the server down, then start the server: no status message (first connection). Stop and
   restart the server at the main menu, then load a save: one status after the load.
4. Loading a save while subtitles are showing: no stale "Recent subtitles" appear afterwards; a bark repeated after the
   load is reported.
5. Run `query_quests` twice within 3 s: the second listing may be dropped (accepted trade-off); after 3 s it arrives.

## Open questions
- Should an explicit query's context bypass the duplicate window? (Needs an API flag; deferred.)
