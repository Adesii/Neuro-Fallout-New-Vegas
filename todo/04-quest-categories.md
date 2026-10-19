# 04 - Quest categories: main / DLC / optional

Status: open

## Problem
`query_quests` and `select_quest` list every quest alike, but quests differ: main story, DLC, and optional/side
quests. Neuro cannot tell what is a main objective.

## Goal
Group (or label) quests by category and make sure Neuro can choose by category or at least see it.

## Notes
- Code: `src/Actions/Gameplay/QueryQuestsAction.*`, `SelectQuestAction.*`, `DoCurrentQuestAction.*`, and the Pip-Boy
  quest handling in `src/Menus/PipBoyHandler.*`.
- Open question to answer first: what in the data distinguishes them? Candidates: the quest form's mod index (DLC
  plugin load-order prefix / master name), the quest flags (e.g. "Start Game Enabled", priority), the Pip-Boy quest
  list order, GECK quest "Priority"/"Event". Do NOT guess flags: check reference repos and `docs/reference-research.md`,
  and the private menu extracts for the Pip-Boy quest tab, and verify in game.
- DLC detection by master file is the most reliable; main vs optional probably needs a quest-priority or name-based
  rule. Ask the designer how Neuro should prefer them (e.g. main first, DLC marked, optional last).
- Keep ordering stable between calls; test the grouping/ordering logic on the host if it is engine-free.

## Done when
Quest lists show a category, ordering follows the designer's preference, and the checklist covers a DLC and a side quest.
