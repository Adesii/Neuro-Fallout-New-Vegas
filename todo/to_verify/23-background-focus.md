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

## Result (2026-10-25, INI toggle superseded on 2026-11-07, see below)
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

## Playtest (designer, 2026-11-07) and change
- Report: the patch seemed to work for one session, then the Pause menu came back on every focus loss and the main menu
  froze again with `bAlwaysActive=1` in the profile's `falloutcustom.ini`.
- Finding: the DLL deployed in `mods/Neuro FNV/nvse/plugins` was dated 2026-10-25 00:59, before the patch commit
  (`caceb02`, 01:46). It has no "Focus loss" strings and `neuro-fnv.log` has no "Focus loss:" line, so the patch never
  ran. The one session where it seemed to work was something else (the pause branch has extra guards: `0x4F1540`, a
  queued action, menus `0x3EF`/`0x3F5`). The main-menu freeze means the INI value never reached the engine either.
- Change: the INI is no longer the toggle. At `kMessage_DeferredInit` the plugin forces the value byte of
  `bAlwaysActive:General` (`0x11DEED8 + 4`) to 1 and always applies the `je` -> `jmp` at `0x86A1F8`. Disassembly check:
  the frame-loop gate (`0x86B371`) and `0x871C90` read the value live through `0x408D60` (returns setting + 4) every frame,
  and the patch-site bytes are `74 42` in the installed `FalloutNV.exe`. Still not run in game.

## Playtest (designer, 2026-11-07, second run) and change
- Report: no Pause menu any more, but the Walker stops walking while the window is unfocused.
- Log: "Focus loss: ... suppressed" is present; the Walker keeps running and holding keys but logs endless
  "no player movement" recoveries, so the held keys never reached the engine.
- Disassembly: the per-frame poll `0xA23010` (called from the main loop at `0x7040FC` and from `0x8674B2`) calls the
  device's `Acquire` and only calls `GetDeviceState` when it returns `DI_OK`/`S_FALSE`; otherwise it zeroes the key and
  button arrays. While unfocused a foreground-cooperative `Acquire` fails (`DIERR_OTHERAPPHASPRIO` under Wine), so the
  hook's `GetDeviceState` (which applies injected input) never ran.
- Change: `FakeDirectInputDevice::Acquire` reports `DI_OK` when the real call fails with `DIERR_OTHERAPPHASPRIO`,
  `DIERR_NOTACQUIRED` or `DIERR_INPUTLOST`. The engine then calls `GetDeviceState`, which already returns no real input
  plus the injected input and retries the real `Acquire` every frame. Builds; not run in game.

## Playtest (designer, 2026-11-07, shared checklist)
Ticked from the older checklist (the one that still had the `bAlwaysActive=0` case):
- With `bAlwaysActive=1` the log says "Focus loss: pause on window deactivation suppressed"; alt-tab away in the world
  gives no Pause menu, and actions and Neuro's input keep working.
- Alt-tab during a menu Neuro is using (container, dialogue, Pip-Boy): nothing closes or opens.
- Focus back: real keyboard and mouse work at once; Escape still opens the Pause menu.
- With `bAlwaysActive=0` the log said the patch was left alone and focus loss behaved as vanilla. That message only
  exists in the build before the INI value was forced, so these ticks are probably from that build. Which build was
  tested is unconfirmed.

Still not run: the `Acquire` change above (walking while unfocused, check 3 below) and the main-menu freeze (check 2).

## Manual in-game checks
1. Rebuild and deploy the DLL first (check the file date in `mods/Neuro FNV/nvse/plugins`). `neuro-fnv.log` then has
   "Focus loss: bAlwaysActive forced to 1 (INI value was N)" and "Focus loss: pause on window deactivation suppressed".
2. Alt-tab away at the main menu: it keeps animating, no freeze.
3. Alt-tab away in the world: no Pause menu, registered actions stay, Neuro keeps acting, and manual action triggers from
   the backend still work. Repeat after opening and closing the console with real input.
   Start a walk (`move_to` or the quest route), then unfocus: the player keeps walking and arrives, no "no player
   movement" recoveries in the log. Typing in the other window must not move the player.
4. Alt-tab during a menu Neuro is using (container, dialogue, Pip-Boy): nothing closes or opens.
5. Focus back: real keyboard and mouse input works again at once.
6. Escape still opens the Pause menu.
7. If the Pause menu still opens in check 3, send the log and tell me what you pressed, so the next suspect (another
   `0x70D*` pause path or a menu reacting to WM_NCACTIVATE at `0x86A267`) can be checked.

## Still open
- Audio on focus loss (itr-nvse `AltTabMute` does the opposite); not changed.
- Do not enable `bBackgroundKeyboard`/`bBackgroundMouse`: they would also feed real keystrokes into the game while typing elsewhere.
