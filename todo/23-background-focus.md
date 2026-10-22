# 23 - Keep the game running when the window is unfocused

Status: open

Goal: the game must not pause when it loses focus (the designer tests with another window in front, and Neuro should play unaffected).

## Done so far
- The DirectInput hook now keeps Neuro's injected input working while unfocused (commit `055a245`).
- The engine setting is `bAlwaysActive:General` at `0x11DEED8`, read in the frame loop gate (`0x86B363`) and the window-activation
  handler (`0x871C90`) of `FalloutNV.exe`. Setting `bAlwaysActive=1` in `[General]` of `Fallout.ini` is untested for New Vegas.

## Playtest (designer, 2026-10-22)
- With the hook fix the game no longer pauses when unfocused and Neuro's input keeps working, but the **Pause menu still
  opens** when the game loses focus. Needs more validation (does `bAlwaysActive=1` change it? which message opens it?).
  Because the pause menu is open, `MenuHandler` may treat it as a blocking menu, so check how Neuro's actions behave then.

## Still open
- If `bAlwaysActive=1` does not stop the pause, have the plugin set it at load and intercept the focus-loss window messages
  (the Nexus mod "Tabbed Out" does this; its page was unreadable here, so check its license/source before borrowing anything).
- Add a config toggle for it; reconsider audio (itr-nvse `AltTabMute` mutes on focus loss, the opposite behaviour).
- Do not enable `bBackgroundKeyboard`/`bBackgroundMouse`: they would also feed real keystrokes into the game while typing elsewhere.
