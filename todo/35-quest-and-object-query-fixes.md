# 35 - Quest routing and object classification fixes

Status: open
Area: `src/GameplayHandler.cpp`, `src/WalkerHandler.cpp`
Source: `docs/code-review.md` D (ResolveQuestTarget, PrepareCurrentQuest, CategoryFor vs IsInteractable), A7 notes.
Related: `todo/04-quest-categories.md`, `todo/07-query-nearby-priority.md`

## Problem
1. `ResolveQuestTarget` returns from inside the loop for the first target that passes the filters
   (`GameplayHandler.cpp:182-202`). If that target lies in another area and `FindEntryDoor` returns null, later valid
   targets of the same objective are never tried.
2. `PrepareCurrentQuest` (`:671-713`) is a two-pass nested loop that calls `ResolveQuestTarget` (itself a loop) for each
   target. Hard to read and easy to break; the "chosen objective first" rule is buried in the pass counter.
3. Two sources of truth for object types: `Walker::IsInteractable` (`WalkerHandler.cpp:110-138`, with an
   `ExtraPrimitive` exclusion) and `GameplayHandler::CategoryFor` (`:229-269`). `query_nearby` can list something Walker
   later calls "not interactable".
4. `query_nearby` returns 30 entries sorted by distance only (`kNearbyLimit`, `:40`); todo 07 asks for 20 by priority and
   a "showing N of M" note.
5. `ObserveQuests` only sees quests still in `player->questObjectiveList`: a "quest completed" transition may be missed if
   the engine removes the objectives with the quest. Unverified, needs a play-through check
   (`GameplayHandler.cpp:408-460`).
6. Minor: `g_questFlags.contains()` then `operator[]`, braces around a one-line `ClearCurrentQuestRoute()`,
   `StartExploration`/`StartObjectAction` repeat the "Could not start..." handling.

## Work
1. Rewrite target resolution as "collect candidate targets (same space first, then via door), then pick": try every
   target before giving up; add the chosen objective as an explicit preference parameter.
2. One object classifier (`WorldUtils::ClassifyObject(ref) -> {category, interactable}`) used by both Walker and the
   scan; decide the `ExtraPrimitive` rule once. Engine-free parts (category names, ordering) host-tested.
3. (Distances in the listing lose their range words, see `39-distances-in-metres.md`.) Scoring/cap from todo 07 with the 20-entry limit (or the cap you decide there); keep ids stable.
4. Quest-completion check: add a debug log + manual step to confirm a completed quest is reported once.
5. Tidy item 6.

## Open questions
- todo 07 says cap 20 with priority; the code uses 30 by distance. Confirm 20 for the final design.

## Done when
A quest with several targets resolves any reachable one, `query_nearby` and Walker agree on interactability, and the
quest-completion behaviour is confirmed in game.
