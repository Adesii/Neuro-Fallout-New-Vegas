# TODO

One file per task, so each can be handed to an agent on its own. Status is in the first line of each file
(`Status: open | in progress | done`). Update it, and delete or move the file when the work is committed.

| # | Task | Area |
| - | ---- | ---- |
| 01 | [crouch_toggle action](01-crouch-toggle.md) | Gameplay actions |
| 02 | [stop_move action](02-stop-move.md) | Walker |
| 03 | [Attacking objects misses (bottles on the fence)](03-attack-object-aim.md) | Combat |
| 04 | [Quest categories: main / DLC / optional](04-quest-categories.md) | Quests |
| 05 | [close_container action](05-close-container.md) | Container |
| 06 | [Periodic status reminders](06-periodic-status.md) | Context |
| 07 | [query_nearby top 20 with priority](07-query-nearby-priority.md) | Gameplay actions |

Notes for whoever picks one up:

- Read `CLAUDE.md` first; for anything touching actions invoke the `neuro-fnv-actions` skill.
- Cloud cannot run FNV: report what was verified, what is assumed, and give a manual in-game checklist.
- Never invent engine addresses; follow `docs/reference-research.md`.
- Add host tests beside any engine-free logic (`bash tools/test.sh`), format with `clang-format -i`, build with `just debug`.
- Commit when a task is done, with explicit paths and the `Co-Authored-By` line.
