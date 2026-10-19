# TODO

One file per task, so each can be handed to an agent on its own. Status is in the first line of each file
(`Status: open | in progress | done`). Update it, and delete or move the file when the work is committed.

| # | Task | Area |
| - | ---- | ---- |
| 01 | [crouch_toggle action](01-crouch-toggle.md) (done, needs in-game test) | Gameplay actions |
| 02 | [stop_move action](02-stop-move.md) (done, needs in-game test) | Walker |
| 03 | [Attacking objects misses (bottles on the fence)](03-attack-object-aim.md) | Combat |
| 04 | [Quest categories: main / DLC / optional](04-quest-categories.md) | Quests |
| 05 | [close_container action](05-close-container.md) (done, needs in-game test) | Container |
| 06 | [Periodic status reminders](06-periodic-status.md) | Context |
| 07 | [query_nearby top 20 with priority](07-query-nearby-priority.md) | Gameplay actions |
| 08 | [In-game verification of the latest changes](08-verify-in-game.md) | Testing |
| 09 | [Hacking menu (terminal lock minigame)](09-hacking-menu.md) | Menus |
| 10 | [Terminal / computers menu](10-terminal-menu.md) | Menus |
| 11 | [Level-up menu](11-level-up-menu.md) | Menus |
| 12 | [Barter / trading menu](12-barter-menu.md) | Menus |
| 13 | [Lockpicking menu](13-lockpick-menu.md) | Menus |
| 14 | [Repair menus (item repair and repair services)](14-repair-menus.md) | Menus |
| 15 | [Weapon mod and crafting recipe menus](15-crafting-menus.md) | Menus |
| 16 | [Sleep / wait menu](16-sleep-wait-menu.md) | Menus |
| 17 | [Casino menus (slot machine, roulette, blackjack)](17-gambling-menus.md) | Menus |
| 18 | [Caravan card game](18-caravan-menu.md) | Menus |
| 19 | [Surgery and medical questionnaire menus](19-surgery-medical-menus.md) | Menus |
| 20 | [Companion wheel](20-companion-wheel.md) | Menus |
| 21 | [Tutorial, book and SPECIAL book menus](21-tutorial-and-books.md) | Menus |
| 22 | [Audit of the remaining menus (Pip-Boy tabs, options, map, credits, loading)](22-remaining-menus-audit.md) | Menus |
| 23 | [Keep the game running when the window is unfocused](23-background-focus.md) | Plugin |

Notes for whoever picks one up:

- Read `CLAUDE.md` first; for anything touching actions invoke the `neuro-fnv-actions` skill.
- Cloud cannot run FNV: report what was verified, what is assumed, and give a manual in-game checklist.
- Never invent engine addresses; follow `docs/reference-research.md`.
- Add host tests beside any engine-free logic (`bash tools/test.sh`), format with `clang-format -i`, build with `just debug`.
- Commit when a task is done, with explicit paths and the `Co-Authored-By` line.
