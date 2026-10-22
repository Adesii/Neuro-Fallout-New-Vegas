# TODO

One file per task, so each can be handed to an agent on its own. Status is in the first line of each file
(`Status: open | in progress | to verify | archived`). A task lives in the folder that matches its state:

| Folder | Meaning |
| ------ | ------- |
| `todo/` | Open or in progress: not implemented, or only partly. |
| `todo/to_verify/` | Implemented and committed, but never run in the game (Cloud cannot run FNV). The task's own `Result` section records what was built, what is unverified, and the manual in-game checks. |
| `todo/archive/` | Implemented and confirmed working in game by the designer. |

Flow: `todo/` -> `to_verify/` when the code is done -> `archive/` only when the designer confirms it works in game.
Use `git mv` and update the `Status:` line and the tables below.

Tasks 24-38 come from `docs/code-review.md` (2026-10-21). Their "Decisions" sections record what the designer chose;
"Open questions" sections must be answered before coding that part.

## Open

| # | Task | Area |
| - | ---- | ---- |
| 03 | [Attacking objects misses (bottles on the fence)](03-attack-object-aim.md) | Combat |
| 04 | [Quest categories: main / DLC / optional](04-quest-categories.md) | Quests |
| 06 | [Periodic status reminders (busy/blocked moments)](06-periodic-status.md) | Context |
| 07 | [query_nearby top 20 with priority](07-query-nearby-priority.md) | Gameplay actions |
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
| 23 | [Keep the game running when the window is unfocused](23-background-focus.md) (game no longer pauses; the Pause menu still opens when unfocused) | Plugin |
| 24 | [Idle poke, post-action wake-up and greeting](24-idle-poke-and-wakeup.md) | Context / scheduling |
| 25 | [Walker stuck detection, recovery, jump, doors, timeouts](25-walker-stuck-recovery.md) | Walker |
| 26 | [Walker interaction line-of-sight and progress/failure feedback](26-walker-interaction-and-feedback.md) | Walker / Context |
| 27 | [Aim unification, stance-aware eye height, safe angle maths](27-aim-unification.md) | Aim / Combat |
| 28 | [Location notices and throttled "you see" push](28-location-and-world-context.md) | Context |
| 29 | [Damage, death and combat-start messages](29-player-status-and-damage-context.md) | Context / Combat |
| 30 | [Context reliability: disconnects, dedupe, subtitles](30-sdk-context-reliability.md) | SDK / Context |
| 31 | [Menu window lifecycle and timeout fixes (popup starvation)](31-menu-window-lifecycle-fixes.md) | Menus (bugs) |
| 32 | [Input injection safety (held keys, pulses, hook)](32-input-injection-safety.md) | Input (bugs) |
| 33 | [Action registry, scripts and tooling correctness](33-action-registry-and-infra-correctness.md) | Infrastructure (bugs) |
| 34 | [Combat engagement timing and small fixes](34-combat-engagement-fixes.md) | Combat (bugs) |
| 35 | [Quest routing and object classification fixes](35-quest-and-object-query-fixes.md) | Gameplay (bugs) |
| 36 | [Cross-module duplication and hygiene](36-shared-utilities-cleanup.md) | Cleanup |
| 37 | [Menu executor refactors (large merges, low priority)](37-menu-executor-refactors.md) | Cleanup |
| 38 | [Engine-free extraction and missing host tests](38-engine-free-extraction-and-tests.md) | Tests |
| 39 | [Report all distances to Neuro in metres](39-distances-in-metres.md) | Context formatting |

## To verify (implemented, waiting for an in-game test)

| # | Task | Area |
| - | ---- | ---- |
| 05 | [close_container action](to_verify/05-close-container.md) (also the stub `close_*` actions) | Container |

## Archive

| # | Task | Area |
| - | ---- | ---- |
| 01 | [crouch_toggle action](archive/01-crouch-toggle.md) | Gameplay actions |
| 02 | [stop_move action](archive/02-stop-move.md) | Walker |

See [archive/](archive/README.md).

Suggested order (from `docs/code-review.md` section F): 24, then 31/32/30 (bugs that can strand her), then 39 (metres,
so later messages use the formatter from the start), then 25/26, 27 together with 03, then 29/28, then 33-35, then
36-38.

Notes for whoever picks one up:

- Read `CLAUDE.md` first; for anything touching actions invoke the `neuro-fnv-actions` skill.
- Features that exist in Skyrim (24, 26, 28, 29, 30): invoke the `skyrim-feature-port` skill and write
  `docs/design/<feature>.md` first.
- Cloud cannot run FNV: report what was verified, what is assumed, and give a manual in-game checklist.
- Never invent engine addresses; follow `docs/reference-research.md`.
- Add host tests beside any engine-free logic (`bash tools/test.sh`), format with `clang-format -i`, build with `just debug`.
- Commit when a task is done, with explicit paths and the `Co-Authored-By` line, and move its file to `to_verify/` (never straight to `archive/`).
