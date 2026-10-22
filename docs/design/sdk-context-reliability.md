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
Conclusion: the engine data does not name the speaker, but it does give where the sound comes from.

## Subtitle speaker: revised decision (designer, 2026-10-22)
Matching the source position to an actor is allowed. Implemented as:
- `SubtitleData::position` is read together with the text for every new subtitle (it is already in the struct the
  handler reads; no hook, no new addresses). `SubtitleEntry` keeps it out of its identity because it follows a moving
  speaker.
- `SubtitleHandler::ResolveSpeaker` collects the live, non-dead actors within 600 units (horizontally) of the source from
  the loaded cells (`WorldUtils::ForEachLoadedReference`) and hands them to the engine-free
  `Menus::SpeakerMatch::Resolve` (host-tested).
- The source can sit anywhere on the speaker (feet, mouth, head), so distance is measured to the actor's body: a vertical
  segment of 160 units times the actor's scale. The best candidate must be within 80 units, and the runner-up at least
  40 units farther; otherwise there is no name (too far, ambiguous or unnamed actor). A missing name is preferred to a
  wrong one, and nothing is guessed beyond the geometry.
- A named match is reported as `- Name: "text"` in the "Recent subtitles" batch; the player can match too.
- Every decision is logged (`Subtitle "..." from (x, y, z): speaker ... (n candidate(s), nearest d, next d2)`) so the
  assumption below can be checked in `Fallout New Vegas` logs.
- Alternative not taken: hooking `Actor::SayTopic` (JIP `GameObjects.h:10`, vtable slot `0x13C`) would give the speaker
  as `this` with certainty, but needs a vtable hook at an address not established in this project. It is the fallback if
  the position turns out not to be the speaker's.

## Unverified assumptions
- `neurosdk_context_connected` turns false promptly after a server restart (the existing disconnect path relies on it).
- Reconnect timing relative to the player being loaded: the status waits for a loaded player, not for a stable frame.
- 3 s is a guess at what repeats are noise.
- **That `SubtitleData::position` is the speaker's position.** JohnnyGuitarNVSE documents it as the source position and
  hooks the call that fills it, but no source here shows what `Interface::ShowText` passes. The 160/80/40 geometry
  numbers are also guesses. The per-subtitle log line exists to verify both.

## Manual in-game checklist
1. Mid-fight, stop the Neuro server: the log shows exactly one "not connected" warning, then none until reconnect.
2. Restart the server during the fight: Neuro receives one "Status after reconnecting" message with health, weapon and
   objectives, and no "Combat update" lines from the outage.
3. Start the game with the server down, then start the server: no status message (first connection). Stop and
   restart the server at the main menu, then load a save: one status after the load.
4. Loading a save while subtitles are showing: no stale "Recent subtitles" appear afterwards; a bark repeated after the
   load is reported.
5. Stand near an NPC who barks (or in a conversation overheard from a distance) and check the log: the position is on
   the speaker (nearest distance well under 80), and "Recent subtitles" names them. Check with two NPCs close together
   and with a radio/Pip-Boy voice (expect no name).
6. Run `query_quests` twice within 3 s: the second listing may be dropped (accepted trade-off); after 3 s it arrives.

## Decisions
- Explicit query results are not exempt from the 3 s duplicate window (designer, 2026-10-22): a repeated identical
  message within 3 s is dropped, including a query result asked for twice.
