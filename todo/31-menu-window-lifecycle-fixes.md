# 31 - Menu window lifecycle and timeout fixes

Status: open
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
