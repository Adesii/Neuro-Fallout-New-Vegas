# 35 - Quest routing and object classification fixes

Status: to verify
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

## Result (2026-10-24)
Built (`src/GameplayHandler.cpp`, `src/utils/WorldUtils.*`, `src/Context/ObjectPriority.*`, `tests/ObjectPriorityTests.cpp`):
1. `CollectQuestSteps` gathers every usable target of an objective that has a way to it (targets in the player's area
   nearest first, then the entry door of targets in other areas; a target whose door is unknown is skipped, not a reason to
   stop). `ResolveQuestTarget` takes the first step. `PrepareCurrentQuest` is now: displayed objectives, the chosen one (and
   inside it the chosen target) first, first objective with a step wins.
2. `WorldUtils::ClassifyObject(ref) -> {category, interactable}` is the one classifier; Walker's `IsInteractable` and the
   scan use it. The `ExtraPrimitive` rule is decided once: loose items with it are neither listed nor interactable; doors,
   containers, activators and the rest of the built-in world are unaffected. The category enum, names and ordering are
   engine-free and tested.
3. `query_nearby` returns at most 20 (todo 07's cap), ranked by importance tier then distance (quest targets first, then
   people, containers, terminals, doors, harvestables, useful items, ..., clutter last), ids follow the ranking, and a
   "Showing N of M, most important first" note appears when something was cut. Quest targets (and the doors toward
   them) are tagged "quest target". The cap of 20 was the todo's open question; I took 20 as the default.
4. Quest completion: `ObserveQuests` logs every completed/failed report and now also looks at quests that vanished from
   the player's objective list, reading the final flags from the quest form, so a completion that removed the objectives is
   still reported once.
5. Tidied: `find` instead of `contains` + `operator[]`, the stray braces, and one `ReportStartFailure` helper for
   `StartObjectAction`/`StartExploration`.

Unverified (in game): that completed quests really are reported once (look for the `Quest completed:` /
`left the objective list` log lines), the importance tiers (a proposal for the designer to tune in `ObjectPriority.cpp`),
and that no object that used to be listed and usable has disappeared (primitives only).

Manual in-game checks: `query_nearby` in Goodsprings shows at most 20 entries with people/containers first, junk last and a
"Showing 20 of N" note; quest targets are tagged; `move_to_object` on any listed id never answers "not interactable";
finish a quest and check the single "Quest completed" message; with a two-target objective one of which is behind an unknown
door, `do_current_quest` still walks to the other.

## Done when
A quest with several targets resolves any reachable one, `query_nearby` and Walker agree on interactability, and the
quest-completion behaviour is confirmed in game.
