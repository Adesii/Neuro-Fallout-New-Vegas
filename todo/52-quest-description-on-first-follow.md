# 52 - Send a quest's description the first time she follows it

Status: open
Area: Context / quests (`GameplayHandler`, quest observation, `do_current_quest`)
Source: designer, 2026-11-09, from reading how the Skyrim integration handles quests
Related: `51-confirm-quest-course-change.md`, `to_verify/41-query-quests-selected-line.md`

## Skyrim behaviour (idea source only)
`get_quest_journal_description_if_never_shown` (`WalkerProcessor.cpp` around line 859, used at 12081 and 12355) sends
the quest's long journal description once, the first time the quest is followed (and again inside the course-change
confirmation). Later follows do not repeat it. Reason: she learns what the quest is about and why she is going
there, without the description cluttering every query or message.

## Here
The listing and notifications carry only `QuestName: objective text` (`ObjectiveText`). Nothing tells her the story of
the quest, so she walks to targets without knowing why.

## Work
1. Find out what FNV offers (**unverified**): objective text is known (`BGSQuestObjective`), but whether a quest has a
   longer journal-style description reachable from `TESQuest` (notes, stage log entries, the Pip-Boy quest page text)
   must be checked in `src/defs/`, NVSE/JG definitions and `docs/reference-research.md` sources before designing. If only
   the objective text exists, the "description" is that text plus the quest name, and this todo shrinks to "say it once
   with context when the follow starts".
2. Remember which quests already had their description shown (set of quest form ids, reset on game load, like the
   other observation state in `GameplayHandler`), in an engine-free helper with host tests.
3. On the first `do_current_quest` for a quest, send the description as a silent context before/with the start message;
   not again on later follows, and not for quests whose description was shown through the course-change window (todo 51).
4. Never send it from the notification path; the `query_quests` result stays short.

## Done when
The first time a quest is followed Neuro gets its description once; following it again does not repeat it.

## Manual in-game checks
1. `do_current_quest` on a fresh quest: description context arrives once.
2. Stop and follow it again: no second description.
3. A different quest: its own description once. After loading a save: shown again once (state not persisted).
