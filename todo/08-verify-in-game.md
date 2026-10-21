# 08 - In-game verification of the latest changes

Status: open

Nothing from the last sessions has been run in the game. Verify and report back (log lines included for failures).

## Checklist
- **crouch_toggle (01):** HUD sneak indicator matches the context message; no key stays held; the Crouch binding toggles
  on a tap. Unverified: the sneak flag `0x400` read.
- **stop_move (02):** works during `move_to_object`, `explore`, a `do_current_quest` route (must not resume), with a menu
  open, and refuses during a melee approach.
- **close_container (05):** closes right after opening; refused while a transfer is queued; Walker resumes.
- **Stub close actions:** open each stubbed menu (barter, lockpick, repair, repair services, weapon mod, recipe,
  sleep/wait, slot machine, roulette, companion wheel, tutorial, book) and call its `close_*`. Check the exit tile
  click closes it, and that the menu IDs 1035 (item repair) and 1058 (repair services) map to the right menus.
- **Unfocused window:** with `bAlwaysActive=1` under `[General]` in `Fallout.ini`, tab out: the game keeps running, Neuro's
  actions keep executing (DirectInput fallback in `src/hooks/Hooks_DirectInput8Create.cpp`), real input works again after
  tabbing in. See task 23 if the game still pauses.
- Earlier combat checklist in `docs/combat.md` is still open.
- **Review follow-ups (`docs/code-review.md` section G):** popup over a dialog / trait screen / tag-skill screen
  (todo 31); walk to an object and wait 30 s without acting (todo 24); crouch then `attack` a bottle (todos 03, 27);
  activate across a railing or floor (todo 26); overlapping input pulses and disconnect/reload mid-fight with no key left
  down (todo 32); quest completion reported once (todo 35).
