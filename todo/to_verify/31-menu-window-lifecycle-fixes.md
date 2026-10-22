# 31 - Menu window lifecycle and timeout fixes

Status: to verify
Area: `src/MenuHandler.cpp`, `src/Menus/*`
Source: `docs/code-review.md` S1, B12 (written by a subagent from a full read; S1 re-checked against the source)

## Problem
1. **S1 (highest):** `MenuHandler::Process` returns early when a popup / text edit / character editor claims the frame,
   so `DialogHandler`, `TraitsHandler` and `CharGenHandler` never run and never close their registered `select_*`
   window. The stale force then blocks the popup's own forced window (process-wide force guard,
   `ActionWindow.cpp`); the popup window retries every 2 s forever and Neuro cannot answer it. Special and VATS already
   receive the popup flags and are fine.
2. `ContainerHandler::AdvanceClose` (`ContainerHandler.cpp:305-326`) has no `IsActive()` guard: its 5 s timeout runs
   while a popup covers the container and it clicks `CM_ExitButton` underneath.
3. `startedAt` is not shifted for pauses in Dialog, Message, Traits, CharGen, Special and Stub (Inventory and
   Container do shift it): resuming after a long obstruction times out immediately, and Special then starts a recovery
   sequence (`SpecialAllocationHandler.cpp:174`).
4. Repeated popup text (`MessagePopupHandler.cpp:154-161`): completion is a changed signature or a closed menu, so an
   identical follow-up popup is never recognised and a timeout is reported. `GetOpenMenu()==null` is also treated as
   "closed" when the popup is merely covered.
5. `ContainerHandler.cpp:398`: `PublishItems` result is ignored after the action result was accepted; a failed send
   leaves the old listing bound and Neuro with no data (`InventoryHandler` retries).
6. Smaller: `TextEditHandler` re-clicks every 1.5 s with no cap and an empty display name blocks silently;
   `CharacterEditorHandler` clicks `RSM_next_button` without `IsControlAvailable`; `DialogHandler` builds its force state
   after `FlushPendingSpeech` so "Spoken dialog since the last choice" is always empty
   (`DialogHandler.cpp:97-120,340`); `SpecialAllocationHandler` sends `completionSummary` twice
   (`:122-128,139-145`) and has function-local statics that `Reset()` does not clear (`:336,349`);
   `CharGenHandler.cpp:292` uses `\\` as path separator where all else uses `/`.

## Work
1. Fix S1 first: call Dialog, CharGen and Traits `Process()` every frame with an `unobstructed` argument as for Special
   and VATS, **or** add `Suspend()` that closes the window and call it from `MenuHandler` whenever an earlier handler
   claimed the frame. Add a case to the manual checklist (popup over dialog / over trait selection / over tag skills).
2. Add an `IsActive()` guard and a timeout that does not run while covered to the container close request.
3. Pause-aware timers everywhere: one shared step timer (engine-free, host-tested). The refactor todo 37 builds on it,
   but do the pause shift now.
4. Popup completion: distinguish "not top" from "gone"; for repeated popups use a click counter or tile pointer.
5. Make `PublishItems` failure retried or reported.
6. The "smaller" list above, one commit each.

## Existing precedent
- Rule in CLAUDE.md: "visual menu execution must lock out window regeneration and Walker until actual completion"; "Do
  not infer menu-open state from retained engine singleton pointers; use top/active menu state."
- `.claude/rules/container-inventory.md`: container close behaviour and the 5 s timeout.

## Done when
A popup appearing over each of the three menus still lets Neuro answer it; no spurious timeouts after obstruction;
popup chains work; tests added for the timer; in-game checklist covers the popup-over-menu cases.

## Result (2026-10-22)
Built and committed (one commit per item). Host tests: 127 pass (new: `StepTimer`); DLL builds. Nothing was run in game.

1. **S1:** `MenuHandler::Process` now calls Special, Dialog, CharGen and Traits every frame with an `unobstructed`
   argument (`!popup && !textEdit && !characterEditor`, plus the existing Special-executing rule) and only then applies
   the popup/text-edit/editor precedence. A covered handler with no running execution closes its window (freeing the
   process-wide force) in the same frame, before `MessagePopupHandler` registers its own; with a running execution it
   pauses. Traits and CharGen additionally had an early `return true` for "visible but not on top" that sat before their
   close branch, so a covered window was never closed; both now call `CloseWindow()` there (found in review, fixed in a
   follow-up commit). Dialog has no such branch.
2. **Container close:** `AdvanceClose` returns while `IsActive()` is false, so Exit is never clicked under a popup.
3. **Timers:** new engine-free `ScopedState::StepTimer` (host-tested): counts only running frames, skips stalls over 1 s
   and the first frame back. Used by Dialog, Message popup, Traits, CharGen, Special (execution and recovery timers),
   Stub close and Container close. Inventory and Container *transfers* keep their existing pause-shifting `startedAt`
   (it works; not migrated).
4. **Popups:** `IsCovered()` (menu still visible but not on top) pauses instead of reporting "closed"; completion after a
   click also needs the clicked button to be gone from the button list, so an identical follow-up popup is recognised.
5. **Container listing:** an accepted query stores a pending request that is retried (1 s apart, 5 attempts, dropped if
   the page is no longer valid) instead of ignoring `PublishItems` failing.
6. **Smaller list:** TextEdit slows its OK retries after 5 clicks (15 s) and warns once without a character name;
   character editor checks `IsControlAvailable` for Next; Dialog keeps flushed speech for the force state until the next
   choice (so "Spoken dialog since the last choice" is no longer always empty); Special reports completion from one
   function and its two function-local statics are namespace-scope and cleared by `Reset()`; CharGen uses `/`.

### Unverified
- `IsCovered()` is `IsMenuVisible(Message)` with something still on top (`GetTopMenuID() != NoMenu`); a visibility flag
  that lingers after the popup closed while another menu is on top would keep Walker blocked until that menu closes.
- The character editor now needs `RSM_next_button` to pass `IsControlAvailable` (visible and targetable, absent traits
  count as available). `race_sex_menu.xml` sets neither trait on the button, but its `text_box.xml` template is not in
  the extracts. If character creation stops advancing at the face editor, remove that check.
- `Menu::IsMenuVisible(Interface::Message)` as the "popup still in the stack" test (same call the other handlers use for
  their own menus) and that a replaced identical popup builds new button tiles. If the engine reuses tiles, an
  identical follow-up still ends as a timeout rather than a mis-detected success.
- The popup sequence for each of the three menus below; none of it can be exercised outside the game.

### Manual in-game checklist
1. Dialog: while Neuro's `select_dialog` window is open, trigger a message popup (e.g. a skill-check or quest
   notification popup that needs a button). Expect: the popup's `choose_popup_option` is forced and Neuro can answer
   it (no repeating "cannot register while another force is active" in the log); after the popup the dialog window
   comes back.
2. Same with a popup over the trait selection and over the tag-skill screen during character creation.
3. A popup that appears while Neuro is mid-click in dialog/traits/tag skills/SPECIAL: the sequence waits and finishes
   afterwards, with no "timed out" message.
4. Two identical popups in a row (e.g. the same warning twice): the second one is answered, no "timed out".
5. Open a container, accept `close_container`, and cover it with a popup before the Exit click: Exit is not clicked
   until the popup is gone.
6. Character creation: the face editor (race/sex menu) advances to the next screen on its own; the name prompt: the name is filled and OK is clicked; with the server's display name missing the log
   warns once.
