# 51 - Ask for confirmation before changing the followed quest

Status: open
Area: Gameplay actions / quests (`SelectQuestAction`, `GameplayHandler::SelectQuest`, `ActionWindow` force)
Source: designer, 2026-11-09, from reading how the Skyrim integration handles quests
Related: `50-quest-route-interrupt-by-move-interact.md`, `to_verify/41-query-quests-selected-line.md`,
`52-quest-description-on-first-follow.md`

## Skyrim behaviour (idea source only)
`change_quest_to_follow` while another quest is being followed does not switch at once. The mod unregisters all
actions and forces a Yes/No choice (`force_type::confirm_change_quest_course`, `WalkerProcessor.cpp` around line 21600)
whose text names both quests: "Previously you were following another quest: <old> <description>. Are you sure you
want to change course and follow new quest: <new> <description>?". Only "Yes" switches; "No" keeps the old route.
Reason: the model hops between quests on a whim and abandons work half done.

## Here
`select_quest` today switches immediately and silently: `GameplayHandler::SelectQuest` stops a Quest-owned Walker
command, clears the route and activates the new quest. Neuro gets only "Selected quest: ...".

## Work
1. Settle with the designer (see questions), then invoke `skyrim-feature-port` and write `docs/design/quest-course-change.md`.
2. When `select_quest` names a **different** quest than the active one **and** a route is running (or recently ran),
   do not switch yet: open a confirmation window (`Actions::ActionWindow` with a force, two choices) that names the
   old and the new quest and each one's objective text. Yes: close/unregister the window, send the result, then switch
   (the required order from `CLAUDE.md`: validate, revalidate, close, `action/result`, execute). No: keep the old quest
   and route, say so.
3. Respect the process-wide force guard in `src/Actions/ActionWindow.cpp` (Neuro supports one active force) and do not
   offer it while another window or menu owns the force. Time out like the other windows and treat a timeout as No.
4. Selecting the quest that is already selected, or any quest while no route runs, switches without asking (but still
   says what changed, see todo 41's wording).
5. Put the decision logic (when to ask, what Yes/No/timeout do) in an engine-free helper with host tests.

## Questions for the designer
- Ask whenever a route runs, or only when the old quest has progressed / was followed for a while?
- A forced window costs an extra round trip and unregisters other actions. Is that acceptable, or should it be a plain
  warning line in the `select_quest` result with the switch still happening?
- Does `move_to`/`interact_with` (todo 50) also get a confirmation? Current decision: no, they just interrupt.

## Done when
Switching quests while one is being followed asks first, naming both; No leaves everything unchanged; Yes behaves as
`select_quest` does today.

## Manual in-game checks
1. Follow quest A with `do_current_quest`, `select_quest` quest B: the confirmation arrives, A keeps walking.
2. Answer No: A continues, nothing changed. Answer Yes: B selected, route stopped.
3. No route running: `select_quest` switches immediately with no question.
4. Ignore the question: it times out as No.
