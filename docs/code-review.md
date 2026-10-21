# Code review, 2026-10-21

Scope: all of `src/` apart from the vendored `itr/` and `defs/`, plus `tests/`, `tools/` and CMake. The Skyrim comparison
covers the walker, aiming and context messages.

How this was produced: I read Walker, Gameplay, Combat, Aim, NeuroSDK, MenuHandler, Stance and plugin.cpp myself. Four
sonnet subagents covered the rest: Skyrim walker, Skyrim context, menu handlers, and infrastructure/hooks/utils.
Nothing was compiled or run; no finding here is runtime-verified.

Verification status of the subagent findings, which I spot-checked against the source:

- Checked by me against the source: S1 (the early-return order in `MenuHandler::Process` and `DialogHandler::Process`),
  B1 (`PulseQueue`), B3 (`~ActionWindow`), B4 (`CachedScripts::Get`), A7.4 (`SubtitleHandler` carries no speaker),
  A6 and B5 (my own reading), and the Walker/Gameplay/Combat items in sections A and D.
- Taken from the subagent reports without a second reading: everything else, notably the Skyrim line references,
  S2, B6-B8, B12, B13 and most of section C.
- Two subagent claims were wrong and are dropped:
  - "`Walker::Pause()` can pass a null player to `SetAutoMove`". `Player::SetAutoMove` null-checks (`src/defs/Player.h:19`).
  - "FNV has no new-objective notice". It does (`GameplayHandler.cpp:431-432`).

Each item gives what is wrong, why, how it is wrong, and how to improve it. "Existing precedent" points at code or
docs in this repo that already says how it should be done. Skyrim paths are relative to
`neuro-fnv-references/Neuro-Skyrim-Integration/src`.

---

## A. Behavioural differences from the Skyrim integration (functionality changes)

CLAUDE.md says Skyrim wins where it disagrees with generic best practice.

### A1. Nothing wakes Neuro after a walk completes or while she is idle (highest impact)

- **What:**
  - Walker `Started` and `Completed` events are sent silent (`GameplayHandler.cpp:496`, `silent = type != Failed`).
  - There is no idle poke anywhere.
  - Walk actions return their `action/result` before the walk starts.
- **Why:** a silent context does not trigger a response. After "Reached X." Neuro has nothing prompting her to act, so
  she can sit idle indefinitely.
- **Skyrim:**
  - `Socket.cpp:1177-1330` pokes at a randomised 6-10 s of idleness. The poke is non-silent, uses a generic phrase from
    a set of six, and carries no state.
  - There is also a 1 s delayed poke after an action completes.
  - Pokes are suppressed while the walker is active or a force-only menu is open.
  - Suggestions were deliberately commented out (`Socket.cpp:1229,1292`). The author found plain nudges sufficient.
- **How to improve:**
  - Add an engine-free `IdlePoker` (timer, jitter, suppression inputs, escalation), host-tested. Precedent:
    `src/Combat/CombatLogic.*` and the CLAUDE.md rule that scheduling/policy lives in an engine-free module.
  - Make walk completion non-silent, or send a short non-silent "what next?" about 1 s after completion.
  - `todo/06-periodic-status.md` says reminders should happen "never during idle gameplay". That contradicts the
    live-tested Skyrim behaviour. Revisit it with the designer.

### A2. Walker has no progress reminder, and its failure messages are generic

- **What:**
  - FNV sends Started, Completed and Failed only.
  - The failure text is always "Could not reach X." (`WalkerHandler.cpp:550`) or "X is no longer available." (`:534`).
- **Skyrim:**
  - A non-silent reminder every 40 s (20 s when exploring), e.g. "You keep following the quest: X. Distance walked N m.
    Walk time S s. Distance to target N m." (`WalkerProcessor.cpp:3975-4105`).
  - A one-time fast-travel hint beyond 400 m.
  - Failure messages name the cause: blocked by an object, not accessible, wrong floor.
- **Why it matters:** FNV wasteland walks are long. The only signal is a silent "Started" message, and then silence.
- **How to improve:**
  - Add a walker status interval (distance remaining, elapsed) from `ProcessWalkerEvents`, excluding `Owner::Combat`.
  - Attach a reason to failures. Walker already knows whether it failed from the stuck check, a missing target, a
    rejected `Activate`, or a vertical mismatch.

### A3. Stuck recovery is weak and frame-rate dependent

- **What:**
  - "Stuck" means at most 1 unit of 2D movement between consecutive frames, accumulated for 3 s (`WalkerHandler.cpp:29-30,321`).
  - "Recovery" lasts 1 s and steers toward `GetPointNavMesh(cell, waypointMarker->GetPos())` (`:555-560`).
  - `waypointMarker->pos` is the last pathing destination. The same route is mostly re-attempted, so recovery is
    effectively a 1 s pause plus a counter reset.
  - After 3 attempts the command fails. That is roughly 12 s with no feedback.
  - There is no jump, sidestep, back-off or door handling.
- **Skyrim:**
  - Window-based stuck: under 50 units in 1.5 s (`detect_stuck`, `WalkerProcessor.cpp:19022-19075`), ignored for 5 s
    after load or teleport.
  - Escalating recovery: a forward step, then a look wiggle across 4 directions (`walk_unstuck` :7584), then a re-path
    (`walk_again` :7426).
  - After repeated failures near a door (within 200 units) it looks at the door and activates it (:25140-25165).
  - A retry budget of 4 (10 for quest walks), then a failure with advice.
  - A jump when the next path points are more than 70 units higher (`needs_jump` :3298).
- **How to improve:**
  - Use a position-over-time window for the stuck check; the per-frame test depends on frame rate.
  - Make recovery escalate: jump (verify the FNV Jump control), strafe/look wiggle, then fresh path, then activate a
    nearby closed door.
  - Skip stuck evaluation briefly after a cell change.
  - Move `UpdateDistanceOscillation`, the stuck window and the recovery state machine into `src/Movement/`. That
    follows `src/Movement/StanceLogic.*` and adds a host test.

### A4. Walker interacts without a line-of-sight or crosshair check

- **What:** `TryComplete` calls `Activate` once the 3D distance is under 100 (`WalkerHandler.cpp:451,482`). The player can
  activate through a floor, wall or railing. If alignment does not happen within 5 s it interacts anyway (`:37,469`).
- **Skyrim:** `interact_with_target` (`WalkerProcessor.cpp:17399+`) requires the real crosshair target to equal the
  target (with bypass flags for special modes).
- **How to improve:**
  - Reuse `CombatHandler`'s `HasLineOfSight` (`CombatHandler.cpp:231`) or the reticle ref. Move it to `WorldUtils` or
    `AimUtils`; combat already needs the same ray.
  - Treat an unobtainable crosshair as stuck, not as a reason to activate.
  - Note that `Intent::Move` completion at "100 units 3D" can also report "Reached" on another floor.

### A5. Doors in the way are not handled

- **What:** only the quest's explicit door step is handled (`GameplayHandler.cpp:144-153`). A closed door between the
  player and a same-cell target is only passed if the pathfinder routes through it, and nothing opens it.
- **Skyrim:** `test_about_to_be_blocked_by_door` (`WalkerProcessor.cpp:4739`) opens an unlocked door when the crosshair
  sits on a closed one for more than 0.2 s.
- **How to improve:** see A3's final recovery step. Reuse `Walker::CanInteract`/`IsInteractable`.

### A6. Aim: fixed heights, inconsistent tables, no lead

- **What:**
  - Combat aims at feet + 70 (character), 35 (creature) or 20 (object), scaled (`CombatHandler.cpp:48-50,161-169`).
  - Walker aims at feet + 100 for any actor, 50 for activators, 20 for flora (`WalkerHandler.cpp:385-403`).
  - `AimUtils::kEyeHeight = 100` is a constant that ignores crouching and creature scale (`AimUtils.hpp:27`).
  - Pitch is computed from `player->pos`, not from the actual camera or weapon origin.
- **Why:** two different answers for "where on a character do I look". The fixed eye height also puts shots high or
  low when crouched. This is a candidate cause for `todo/03-attack-object-aim.md` (bottles missed): a 20-unit object
  height, fixed eye height, and `kRangedPitchTolerance = 0.07 rad` (about 4 degrees, roughly 35 units at 500 units)
  together swamp a bottle.
- **Skyrim:** `lock_camera_onto_target` (`WalkerProcessor.cpp:5447`) uses the bounding-box height, per-object shifts,
  head or torso nodes for actors, target-velocity lead using projectile speed (:5700-5740), and arc compensation for
  bows (:5745-5760).
- **How to improve:**
  - One `AimUtils::TargetPoint(ref, purpose)` used by both Walker and Combat.
  - Derive the eye height from stance (crouching changes it) instead of the constant.
  - Add projectile lead for slow projectiles only.
  - Collect the in-game data `todo/03` asks for before changing constants.
  - Bounding-box and skeleton node access are unverified in this codebase; check `docs/reference-research.md` first.

### A7. Context message gaps versus Skyrim

All of these are unverified for FNV equivalents. The first three matter most.

1. **Location/area change.** Skyrim sends `[Current location: X ...]` (`main.cpp:1861`). FNV never mentions the cell
   name. `PlaceName()` already exists in `GameplayHandler.cpp:163`.
2. **Greeting and re-greeting.** Skyrim sends a silent greeting on connect and every 600 s (`Socket.cpp:468,1203`).
   FNV sends only the SDK Startup message (`NeuroSDK.cpp:195`).
3. **Damage taken and death.** Skyrim sends "X hits you with W", silent, once per attacker per 10 s
   (`Observer.hpp:154-177`), and a death message (`Observer.cpp:9146`). FNV has banded health notes inside a combat
   context only (`CombatLogic.cpp:322-364`). There is no death message and no out-of-combat damage.
4. **Speaker names.** Skyrim sends `Name[Emotion] says "text"`, silent and immediate (`main.cpp:1032-1096`).
   `SubtitleHandler` sends `## Recent subtitles` / `- "text"`, silent, batched for 5 s, with no speaker
   (`SubtitleHandler.cpp:33-42`). The struct read there only exposes text, start time and priority. Neuro cannot tell
   who is speaking.
5. **Proactive "You see:" notices.** Skyrim pushes newly seen interesting objects (`Observer.cpp:3599-4036`). FNV is
   pull-only (`query_nearby`). If this is wanted, build it on the scoring from `todo/07`.
6. **Dedupe and rate-limit filter in front of `SendContext`.** Skyrim drops duplicate or junk messages
   (`main.cpp:1104-1130`, `Socket.cpp:1190`). FNV has no generic filter.
7. **Message weight.** The combat-start message is a non-silent block with enemy list, player status and instructions
   (`CombatHandler.cpp:555-570`). Skyrim keeps similar messages to one or two short sentences. The action descriptions
   already carry the "use these ids with attack" instruction.

### A8. Things FNV does better and should keep

- Strafe and back keys while turning, so the player does not stall to rotate.
- The clamped frame delta.
- The owner/event model for Walker commands.
- The door-step wording in quest routes (`GameplayHandler.cpp:486-495`).
- Structured Markdown context.

---

## B. Real bugs and latent hazards

### S1. A stale forced window blocks the popup window (Dialog / Traits / CharGen)

- **Where:** `MenuHandler.cpp:59-76`, `DialogHandler.cpp:317-363`, `TraitsHandler.cpp:418-441`,
  `CharGenHandler.cpp:426-449`.
- **What:**
  - `MenuHandler::Process` returns early as soon as `MessagePopupHandler`, `TextEditHandler` or `CharacterEditorHandler`
    claims the frame.
  - `DialogHandler`, `TraitsHandler` and `CharGenHandler` close their own window only inside their own `Process()`.
  - While a popup is on top, none of these three run, so a registered `select_*` window stays registered.
- **Why:**
  - A popup opens over a dialog, trait screen or tag-skill screen. The old window and its force stay active.
  - The process-wide force guard (`ActionWindow.cpp`, rule in CLAUDE.md) then rejects the popup's own forced window.
    The popup window goes Faulted and retries every 2 s.
  - Neuro keeps a force on an action that fails validation and cannot answer the popup.
  - `SpecialAllocationHandler` and `VatsHandler` already get `!popupPresent && ...` flags, which is why they do not
    have the problem.
- **How to improve:**
  - Call all three `Process()` every frame with an `obstructed` argument, as is done for Special and Vats.
  - Or have `MenuHandler` call a `Suspend()` on each of them whenever an earlier handler claimed the frame.
  - Needs an in-game check.

### S2. `SubtitleHandler` is never reset

- **Where:** `SubtitleHandler.cpp` state (`g_remembered`, `g_pending`, `g_flushScheduled`), `MenuHandler::Reset`
  (`MenuHandler.cpp:19-31`).
- **What:** state survives disconnect, load and new game.
- **Why:** old subtitles can flush into a new session. The dedupe key includes `startTime`, which restarts after a
  load, so a bark with identical text can be dropped.
- **How:** add `SubtitleHandler::Reset()` and call it from `MenuHandler::Reset`.

### B1. `PulseQueue` can release a key another pulse still needs

- **Where:** `src/utils/InputUtils.cpp:32-46`.
- **What:** a second `Pulse()` on a macro that is already pulsing pushes a second entry. When the first expires,
  `SetControlHeld(false)` runs while the second still has time. The hook's `hold` is a plain bool, not a counter.
- **Why:** any two owners sharing a macro can cancel each other. Walker's `heldMovementKeys` and the combat/stance/VATS
  queues can all touch the same movement or attack macro.
- **How:** refresh the existing entry instead of pushing a duplicate, and release only when no live entry has that
  macro. Better, make the hook hold a counter.

### B2. No central "release all injected input"

- **Where:** `NeuroSDK.cpp:183-192`, `Hooks_DirectInput8Create.cpp`.
- **What:** six independent owners release their own keys: Walker, the combat attack macro, and the pulse queues in
  Combat, Stance and Vats. `ResetAutomation` relies on each to clear itself.
- **Why:** `.claude/rules/combat.md` and `CombatHandler.cpp:144` already call a stuck mouse button the worst failure,
  and one missed path leaves a key stuck.
- **How:** add `DIHookControl::ClearAllScripted()` (held states, taps, buffered queue). Call it at the end of
  `ResetAutomation` and on pre-load.

### B3. `ActionWindow` destructor leaves registry entries

- **Where:** `ActionWindow.cpp:11-14`.
- **What:** `~ActionWindow` only clears the force guard. `ActionRegistry` keeps raw `IAction*` and `ActionWindow*`.
- **Why:** a handler that does `g_window.reset()` on a Registered or Forced window without `Release()`/`End()` makes
  the next `Dispatch` call `Validate()` on freed memory. The destructor comment is about static teardown, but the same
  path runs at runtime. The test only covers the force guard.
- **How:** unbind in the destructor for Registered/Forced/Closing states, guarded for static teardown.

### B4. `CachedScripts::Get` caches a null script forever

- **Where:** `CachedScripts.hpp`, `Get`.
- **What:** `try_emplace(source)` inserts null before `g_script` is checked. One early call, or one failed compile,
  returns null for every later call and never retries. The map is keyed by `std::string`, so each lookup allocates.
- **How:** insert only on success, or keep a failed set with a backoff. Key by `const char *` or `string_view`.

### B5. `WrapAngle` loops can hang on infinity

- **Where:** `AimUtils.cpp:25-32`, `math.cpp:406-409` (two copies).
- **What:** `while (a > pi) a -= 2*pi` never ends for `+inf`, and NaN passes through unwrapped. The inputs are engine
  floats.
- **Why:** this is a case where a defensive check is justified. A hang in the main-loop callback freezes the game.
- **How:** use `std::remainder`, with one shared implementation.

### B6. `DeliverOrQueue` closes the window before the empty-id check

- **Where:** `ActionRegistry.cpp:86-96,118-122`.
- **What:** a request with an empty id ends the window and is never answered or executed.
- **How:** reject up front, before `Validate`, without touching the window. Low risk because the SDK always supplies ids.

### B7. `RetryPendingResults` is not re-entrancy safe

- **Where:** `ActionRegistry.cpp:150-157`.
- **What:** `execute()` runs after `erase(iter)` while the loop continues with `iter`. If the callback causes `push_back`
  or `Reset`, the iterator is invalidated.
- **How:** move finished entries to a local vector and execute after the loop.

### B8. Non-action SDK messages are never destroyed

- **Where:** `NeuroSDK.cpp`, `PollMessages`.
- **What:** `neurosdk_message_destroy` is called only for Action messages. The pinned library frees only Action
  messages today, so nothing leaks, but a submodule bump could change that.
- **How:** destroy every polled message, or comment the assumption.

### B9. Context sent while disconnected is lost, with a warning on every call

- **Where:** `NeuroSDK.cpp:208-212`. Most callers ignore the bool (`CombatHandler.cpp:422,528,570,582`,
  `GameplayHandler.cpp:458,496`).
- **What:** the "NeuroSDK is not connected" warning logs on every call. `FlushNotes` clears the pending notes and sets
  the cooldown before knowing the send succeeded (`CombatHandler.cpp:522-528`).
- **Why:** quest completions and "combat ended" can be dropped during a reconnect.
- **How:**
  - Keep a small bounded queue for non-silent context while disconnected and replay it on reconnect, or at least
    rate-limit the warning.
  - Make `FlushNotes` clear only on success.

### B10. Combat engagement timing

- **First shot delay.** `Engagement::Begin` zeroes `m_pulseTimer`, so the first pulse waits the full "off" time:
  0.16 s ranged, 0.4 s melee, 0.9 s thrown (`CombatLogic.cpp:269-282`). Start the timer at the off threshold.
- **Stall timer while approaching.** `m_stallTime` counts while combat's own melee approach walks
  (`CombatLogic.cpp:249-253`), so a long approach ends as `Stalled` after 20 s. This is already listed as unverified
  item 8 in `docs/combat.md`. Pause the timer while `output.approach` is true.
- **Idle churn.** `ProcessEngagement` calls `ReleaseInputs()` and `Walker::SetLookSuppressed(false)` every frame when no
  engagement is active (`CombatHandler.cpp:438-442`). Harmless, but it sends `SetControlHeld(kNoControl)` every frame.
  Release on the transition only.
- **Out of ammo.** `OutOfAmmo` needs both `clip == 0` and `reserve == 0`. If the HUD text is absent, `reserve` is -1 and
  the engagement waits for the 20 s stall instead.

### B11. Persistent action sets re-register every frame with no backoff

- **Where:** `PersistentActionSet.cpp:16-55`; callers in Container, Inventory, PipBoy and Stub handlers.
- **What:** when `Register()` fails (name collision, SDK error), each frame rebuilds definitions, re-serialises schemas
  and logs a warning.
- **Existing precedent:** `ScopedState::Backoff`, used by action windows and already tested.

### B12. Menu timeouts and pausing

- **Container close request** (`ContainerHandler.cpp:305-326`) has no `IsActive()` guard. It keeps its 5 s timeout and
  clicks `CM_ExitButton` underneath a popup, so a covered container can be reported as "did not close".
- **`startedAt` is not shifted for pauses** in Dialog, Message, Traits, CharGen, Special and Stub. Inventory and
  Container do shift it. Resuming after a long obstruction can time out immediately.
- **Repeated popup text** (`MessagePopupHandler.cpp:154-161`): completion is detected by a changed signature or a
  closed menu. If the same popup immediately follows, the click is never recognised and a timeout is reported.
  `GetOpenMenu() == null` is also treated as "closed" when the popup is only covered by another menu.
- **Query publish failure is silent** (`ContainerHandler.cpp:398`): `PublishItems` is ignored after the action
  result was already accepted. `InventoryHandler` retries.
- **Lower severity:**
  - `TextEditHandler` re-clicks every 1.5 s with no cap.
  - `CharacterEditorHandler` clicks `RSM_next_button` without `IsControlAvailable`.
  - `DialogHandler` builds its force state after `FlushPendingSpeech`, so the "Spoken dialog since the last choice"
    section is always empty.

### B13. Latent input-buffer issues in the hook (not used yet)

- **Where:** `Hooks_DirectInput8Create.cpp:210-212,514-565`.
- **What:** `GetDeviceData` ignores `m_deviceType`, and `m_bufferedPresses` is never cleared on reset and is unbounded.
- **Why:** nothing calls `BufferedKey*` today. Fix this before first use.
- **Also:** `ReadDeviceState` re-acquires devices the game released on purpose (`:127-136`). Add alt-tab, console and
  minimise to the manual checklist.

### B14. Tooling

- `tools/test.sh:8` redirects the build output to `/dev/null`. A compile error exits non-zero under `set -e` with no
  message.
- `CMakeLists.txt:38-41`: the second glob `src/*/*.cpp` is redundant, because `GLOB_RECURSE` already recurses. No
  warning flags are set.

---

## C. Duplicated code

| # | Duplication | Locations | Existing helper / suggested home |
|---|---|---|---|
| C1 | `FrameTime()` clamp of `secondsPassed` to 0..0.1 | `WalkerHandler.cpp:140`, `CombatHandler.cpp:659` (exported and used by `VatsHandler.cpp:449`), `StanceHandler.cpp:34`, `GameplayHandler.cpp:516-518`, `DelayedGuard.cpp:19` (unclamped, no null check) | One `TimeUtils::FrameSeconds()`. A menu should not depend on `CombatHandler` for a clock. |
| C2 | Traits vs CharGen executors, about 400 lines, plus `SelectTrait`/`SelectSkill` and the two `Done*` actions | `TraitsHandler.cpp:28-476` vs `CharGenHandler.cpp:31-485` | A shared tag-selection executor parameterised by list box, selected-state accessor and done control. |
| C3 | Dialog vs MessagePopup select, wait 1.5 s, verify, click flow, and the window-sync block in Dialog, Message, Traits, CharGen, Special and Vats | `DialogHandler.cpp:162-227,325-361`, `MessagePopupHandler.cpp:128-191,278-315` and the others | `Menus::PublishedWindow::Sync(signature, factory)` on top of `ScopedState::Backoff`. |
| C4 | `IsTopMenu` copied four times | `DialogHandler.cpp:59`, `TraitsHandler.cpp:66`, `CharGenHandler.cpp:69`, `SpecialAllocationHandler.cpp:78` | `UIUtils::IsTopMenu` already exists and is used by Message/TextEdit/CharacterEditor/Vats. |
| C5 | Close request and exit button | `ContainerHandler.cpp:46-50,285-326` vs `StubMenuHandler.cpp:241-315` | A shared native-exit helper. |
| C6 | `IsControlAvailable && ClickTile` | Container, Stub, Vats `ClickIfAvailable`, Inventory | `UIUtils::ClickAvailable`. |
| C7 | Container vs Inventory skeleton: pause/timeout block, quantity-prompt result switch, "Query types" help text | `ContainerHandler.cpp:160-212,109-115` vs `InventoryHandler.cpp:433-480,367-371` | An engine-free step timer (testable) and one shared help constant. |
| C8 | Item and query actions | `LootItem`/`StowAwayItem`, `Equip`/`Unequip`/`Use`/`Drop`, and the three Query actions | Parameterised `IndexAction` / `QueryAction`. |
| C9 | "Valid ... ids are" builders | `GameplayHandler.cpp:380-406` (two), `CombatHandler.cpp:390-402` | One `FormatValidIds(label, ids, hint)` with a host test. |
| C10 | The 5-line revalidation lambda, 10 copies | Gameplay and Combat actions | A helper in `Action.hpp`. |
| C11 | `PersistentActionSet` built-flag plus register/unregister boilerplate | Combat, Stance, Gameplay, Container, Inventory, PipBoy | `PersistentActionSet::Sync(bool wanted)` plus lazy build. |
| C12 | Game-ready probe via `ValidateQuestAction(ignored)` | `StanceHandler.cpp:56`, `CombatHandler.cpp:667,863` | Expose `GameplayHandler::IsGameReady()`. |
| C13 | Pitch formula and geometry | `WalkerHandler.cpp:405-411` vs `AimUtils::DesiredPitch`; inline 3D/2D lengths | Use `WorldUtils::Distance3D` and `AimUtils::DesiredPitch`. |
| C14 | Three turn controllers with the same constant name and different values | `WalkerHandler.cpp:46-51` (`kMaxTurnRate = 3`), `AimUtils.cpp:21-23` (`kMaxTurnRate = 7`) | Name them after the use, or share a smoother. |
| C15 | Shared constants re-declared | `kStepDelay` 350 ms in four files, `kRegisterRetryDelay` 2 s in six, `kCloseTimeout` 5 s in two, `using Clock` in eight files | `SKILL.md` asks to share constants. `ScopedState::Clock` already exists. |
| C16 | Object type lists: `Walker::IsInteractable` and `GameplayHandler::CategoryFor` | `WalkerHandler.cpp:110-138`, `GameplayHandler.cpp:229-269` | They disagree (for example `ExtraPrimitive` exclusion). `query_nearby` can list something Walker later calls "not interactable". Share one classifier. |

---

## D. Over-defensive, ugly, or unclear code

1. **`WalkerHandler.cpp:426-448`** `UpdateMovement` calls `ReleaseMovementKeys()` every frame and re-holds the same
   keys. It only needs to change on transitions.
3. **`WalkerHandler.cpp:243-249`** a repath is forced whenever the player is within 60 units of the cached waypoint.
   If the pathfinder returns a very close `nodes[1]`, this repaths every frame, which the 0.25 s interval was meant to
   prevent. Add a floor interval and skip nodes already within reach.
4. **`GameplayHandler.cpp:182-202`** `ResolveQuestTarget` returns from inside the loop on the first target that passes
   the filters. If that target is in another area and `FindEntryDoor` returns null, later valid targets are never
   tried.
5. **`GameplayHandler.cpp:688-708`** `PrepareCurrentQuest` is a two-pass nested loop that calls `ResolveQuestTarget`
   (which loops targets again) for each target. It would read better as "collect candidates, prefer the chosen one".
6. **`GameplayHandler.cpp:752,40`** `query_nearby` returns 30 entries sorted by distance only. `todo/07` asks for 20
   by priority and for a "showing N of M" note. This is known and open.
7. **`Walker::Start` for `Owner::Combat`** pushes a `Started` event each time (`WalkerHandler.cpp:509`), and
   `ProcessWalkerEvents` throws it away (`GameplayHandler.cpp:471`). Combat events could be skipped at the source.
8. **`CombatLogic.cpp:74-90`** raw weapon-type integers (0, 1, 2, 10-13) with no names. `CombatLogic.cpp:331` has
   `band >= 4/3/2` coupled to the 4-element threshold table.
9. **`math.h:6`** `const double PI = std::acos(-1.0)` is dynamically initialised per translation unit, and the header
   does not include `<cmath>`. Use `std::numbers::pi_v<float>`.
10. **`Hooks_DirectInput8Create.cpp:348`** the bare `0x00FDF02C` thunk address should be named and cited to 1.4.0.525.
    `<deque>` is included twice, and `FramerateTracker::Update` runs on every mouse poll with no reader.
11. **Raw addresses in handlers:** `DialogHandler.cpp:57` (`0x11D9510`) and `TraitsHandler.cpp:64` (`0x11DAF74`). The
    other handlers use `src/defs/` wrappers.
12. **`UIUtils.cpp:132-133`** `strcpy_s(path, name)` into a 256-byte buffer invokes the CRT invalid-parameter handler on
    overflow. Use a bounded copy.
13. **Plain-text context messages** where `SKILL.md` asks for `##` Markdown: `MessagePopupHandler.cpp:119,239`,
    `DialogHandler.cpp:144,293`, `TraitsHandler.cpp:188,381`, `CharGenHandler.cpp:190,389`,
    `SpecialAllocationHandler.cpp:102,312`.
14. **`plugin.cpp:35-104`** is a switch with about 25 empty cases. Keep only the cases that act.
15. **`CharGenHandler.cpp:292`** uses `\\` as the tile-path separator where everything else uses `/`.
16. **`ActionData::GetInteger`/`HasProperty`/`GetString`** each re-walk the elements with the same key match. Extract
    `FindElement`. `GetInteger` cannot tell wrong type from out of range.
17. **`SelectQuestAction`/`TargetObjectAction`** parse a single id by hand and neither enforces `id >= 1`, although the
    schema says minimum 1.

---

## E. Tests and engine-free logic

1. **Walker's stuck, oscillation and recovery logic is pure arithmetic inside `WalkerHandler.cpp`.** It is untestable
   and is the part Skyrim spent the most effort on. Extract it into `src/Movement/` like `StanceLogic`.
2. **The fake transport is more lenient than the real one** (`tests/FakeNeuro.cpp`): the real `RegisterActions` rejects
   an empty list and a non-object root schema, `SendActionResult` refuses an empty id, `ForceActions` refuses empty
   names, and `UnregisterActions` on an empty list sends nothing. The stub signature list is hand-copied and can drift
   from `src/NeuroSDK.hpp`.
3. **Untested pure logic:** `PulseQueue`, the angle wrap and heading math, the three `Valid*Ids` builders,
   `RetryPendingResults` re-entrancy, `ActionData::GetInteger` edge cases (`-0`, `1.0`, `1e2`), and destroying a bound
   window.
4. **`Inventory::FormatItemPage` and `ValidateMenuPage`** are engine-free but live in `MenuItems.cpp`, which is not in
   the test build. Move them to `ItemListing`/`ItemQuery`.
5. **`JsonSchema::AppendNumber`** can emit `nan`/`inf`, which is invalid JSON.

---

## F. Suggested order of work

1. A1 and A7.3 (idle/post-walk poke, non-silent completion). Biggest effect on whether she keeps playing.
2. S1 (popup starvation) and S2, B1/B2 (input release), B9 (context loss).
3. A3/A4/A5 (stuck recovery, interaction line of sight, doors), preceded by extracting the stuck logic with tests.
4. A6 (single aim-point table, stance-aware eye height), together with `todo/03`'s in-game measurements.
5. A2 and the remaining A7 items (progress reminders, location, damage, speaker names).
6. The duplication clean-ups in section C. C1, C4, C9, C12 and C15 are mechanical. C2 and C3 are the large ones.

## G. In-game checks for the open items

- Popup over a dialog, trait screen or tag-skill screen: does the popup window register, and does Neuro get a force on
  it? (S1)
- Walk to an object, wait 30 s without Neuro acting. Is there a prompt? (A1)
- Crouch, then `attack` a bottle. Does the aim error change with stance? (A6, `todo/03`)
- Make the walker interact across a railing or floor. Does it activate? (A4)
- Hold two actions that use the same control for less than 0.2 s apart. Is the key released early? (B1)
- Disconnect and reconnect the SDK mid-fight, with and without a held attack button. (B2, B9)
