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
| 04 | [Quest categories: main / DLC / optional](04-quest-categories.md) (grouped listing and DLC tags done, to verify; main-quest list needs game data) | Quests |
| 06 | [Periodic status reminders (busy/blocked moments)](06-periodic-status.md) | Context |
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
| 37 | [Menu executor refactors (large merges, low priority)](37-menu-executor-refactors.md) (3, 4, 6 done; 1, 2, 5 left on purpose) | Cleanup |
| 40 | [Opening a container leaves Neuro idle](40-container-open-poke.md) (container done, to verify; 2026-11-09 follow-up: slim opening text, no container-specific poke; Pip-Boy open) | Context / Menus |
| 42 | [attack with no weapon equipped only looks at the target](42-attack-without-weapon.md) | Combat |
| 44 | [Debug camera: free or top-down view while the Walker runs](44-debug-camera.md) | Tooling |
| 45 | [Custom navigation grid from raycasts, with A*](45-custom-navigation-grid.md) (first stage built and on by default; first run found no way through the Primm wrecks, correctly: on-demand ray grid with A*, `NWalkerGrid` to try it; saving, sampling ahead and long routes not built) | Walker / pathing |
| 47 | [Aim down sights automatically past 10 meters](47-auto-aim-down-sights.md) | Combat |
| 49 | [Fix JSON string escaping upstream in libneurosdk](49-libneurosdk-json-escaping.md) (later; worked around in the plugin) | SDK / transport |
| 50 | [move_to / interact_with interrupt the do_current_quest route](50-quest-route-interrupt-by-move-interact.md) | Walker / Gameplay actions |
| 51 | [Ask for confirmation before changing the followed quest](51-confirm-quest-course-change.md) (idea from Skyrim) | Gameplay actions |
| 52 | [Send a quest's description the first time she follows it](52-quest-description-on-first-follow.md) (idea from Skyrim) | Context |
| 53 | [Custom (plugin-defined) quests](53-custom-quests.md) | Quests |
| 54 | [Plan long routes over the game's map of navmeshes](54-long-range-navmesh-map.md) (research first) | Walker / pathing |

## To verify (implemented, waiting for an in-game test)

| # | Task | Area |
| - | ---- | ---- |
| 25 | [Walker stuck detection, recovery, jump, doors, timeouts](to_verify/25-walker-stuck-recovery.md) (stricter recovery after the 2026-11-07 playtest) | Walker |
| 26 | [Walker interaction line-of-sight and progress/failure feedback](to_verify/26-walker-interaction-and-feedback.md) | Walker / Context |
| 29 | [Damage, death and combat-start messages](to_verify/29-player-status-and-damage-context.md) (cause/lesson texts are drafts) | Context / Combat |
| 32 | [Input injection safety (held keys, pulses, hook)](to_verify/32-input-injection-safety.md) | Input (bugs) |
| 34 | [Combat engagement timing and small fixes](to_verify/34-combat-engagement-fixes.md) | Combat (bugs) |
| 41 | [query_quests says which quest is selected](to_verify/41-query-quests-selected-line.md) (top line, "(selected)" tag, route state) | Gameplay actions |
| 43 | [In-game Walker debug mode: show the computed path](to_verify/43-walker-debug-path-overlay.md) (`NWalkerDebug` console command, D3D9 overlay) | Walker / tooling |
| 48 | [Check the path ahead with raycasts and detour around blockers](to_verify/48-raycast-path-detours.md) (on by default since 2026-11-12, `NWalkerDetour 0` switches it off; side-step did not help at the Primm car; rerouting reacts but twice left a way through the wrecks; now a loop that asks the pathfinder until its route is ray-clear or fails the walk as blocked, 2026-11-12, not yet run) | Walker / pathing |

## Archive

| # | Task | Area |
| - | ---- | ---- |
| 01 | [crouch_toggle action](archive/01-crouch-toggle.md) | Gameplay actions |
| 02 | [stop_move action](archive/02-stop-move.md) | Walker |
| 03 | [Attacking objects misses (bottles on the fence)](archive/03-attack-object-aim.md) | Combat |
| 05 | [close_container action](archive/05-close-container.md) (also the stub `close_*` actions) | Container |
| 07 | [query_nearby top 20 with priority](archive/07-query-nearby-priority.md) | Gameplay actions |
| 23 | [Keep the game running and unpaused when the window is unfocused](archive/23-background-focus.md) (forces `bAlwaysActive` on and patches the window procedure's pause on focus loss; INI no longer matters) | Plugin |
| 24 | [Idle poke, post-action wake-up and greeting](archive/24-idle-poke-and-wakeup.md) (phrase list is a draft) | Context / scheduling |
| 27 | [Aim unification: camera origin, one target point, safe angle maths](archive/27-aim-unification.md) | Aim / Combat |
| 28 | [Location notices and throttled "you see" push](archive/28-location-and-world-context.md) | Context |
| 30 | [Context reliability: disconnects, dedupe, subtitles](archive/30-sdk-context-reliability.md) | SDK / Context |
| 31 | [Menu window lifecycle and timeout fixes (popup starvation)](archive/31-menu-window-lifecycle-fixes.md) | Menus (bugs) |
| 33 | [Action registry, scripts and tooling correctness](archive/33-action-registry-and-infra-correctness.md) | Infrastructure (bugs) |
| 35 | [Quest routing and object classification fixes](archive/35-quest-and-object-query-fixes.md) | Gameplay (bugs) |
| 36 | [Cross-module duplication and hygiene](archive/36-shared-utilities-cleanup.md) | Cleanup |
| 38 | [Engine-free extraction and missing host tests](archive/38-engine-free-extraction-and-tests.md) | Tests |
| 39 | [Report all distances to Neuro in metres](archive/39-distances-in-metres.md) | Context formatting |
| 46 | [query_nearby: pages past the first 20, and search by name](archive/46-query-nearby-paging-and-search.md) | Gameplay actions |

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
