# 23 - Keep the game running when the window is unfocused

Status: to verify

Goal: the game must not pause when it loses focus (the designer tests with another window in front, and Neuro should play unaffected).

## Done so far
- The DirectInput hook now keeps Neuro's injected input working while unfocused (commit `055a245`).
- The engine setting is `bAlwaysActive:General` at `0x11DEED8`, read in the frame loop gate (`0x86B363`) and the window-activation
  handler (`0x871C90`) of `FalloutNV.exe`. Setting `bAlwaysActive=1` in `[General]` of `Fallout.ini` is untested for New Vegas.

## Playtest (designer, 2026-10-22)
- With the hook fix the game no longer pauses when unfocused and Neuro's input keeps working, but the **Pause menu still
  opens** when the game loses focus. Needs more validation (does `bAlwaysActive=1` change it? which message opens it?).
  Because the pause menu is open, `MenuHandler` may treat it as a blocking menu, so check how Neuro's actions behave then.

## Result (2026-10-25)
- Disassembly of `FalloutNV.exe` 1.4.0.525 explains the playtest: `bAlwaysActive` is only read in the frame-loop gate
  (`0x86B363`) and in `0x871C90` (the active-flag setter). The pause on focus loss is a separate call in the window
  procedure (`0x86A0A0`), WM_ACTIVATE with state WA_INACTIVE: `call 0x709470` with action 6 at `0x86A234`, guarded by
  `0x4F1540`, no queued action and neither menu `0x3EF` nor `0x3F5` open. Action 6 runs `0x7CB7D0` (pause, time stamps) and
  then HUD mode 4 (`0x771700`). The bAlwaysActive call `0x871C90(0)` comes after it, so the setting cannot stop it.
- New `src/hooks/Hooks_FocusLoss.*`, called from `kMessage_DeferredInit`: when `bAlwaysActive=1` it turns the `je` at
  `0x86A1F8` into `jmp` (`74 42` to `EB 42`), skipping the whole pause block and still running `0x871C90(0)`. It verifies
  the two bytes first and logs a skip otherwise. The engine setting is the toggle; there is no plugin config. Changing
  `bAlwaysActive` at runtime does not install or remove the patch.
- Unverified: that the Pause *menu* you saw is opened by this action and not by something later in that path. All
  addresses come from the disassembly only. Escape and other pause paths are untouched. Not run in game.
- Windows minimise (the HIWORD path, `0x86A187`) is unchanged.

## Manual in-game checks
1. `bAlwaysActive=1`: `neuro-fnv.log` has "Focus loss: pause on window deactivation suppressed". Alt-tab away in the world:
   no Pause menu, registered actions stay, Neuro keeps acting, and manual action triggers from the backend still work.
2. Alt-tab during a menu Neuro is using (container, dialogue, Pip-Boy): nothing closes or opens.
3. Focus back: real keyboard and mouse input works again at once.
4. Escape still opens the Pause menu.
5. `bAlwaysActive=0`: the log says the patch was left alone and focus loss behaves as vanilla.
6. If the Pause menu still opens in check 1, send the log and tell me what you pressed, so the next suspect (another
   `0x70D*` pause path or a menu reacting to WM_NCACTIVATE at `0x86A267`) can be checked.

## Still open
- Audio on focus loss (itr-nvse `AltTabMute` does the opposite); not changed.
- Do not enable `bBackgroundKeyboard`/`bBackgroundMouse`: they would also feed real keystrokes into the game while typing elsewhere.
