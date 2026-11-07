# 46 - query_nearby: see past the first 20, and search by name

Status: archived (verified in game, 2026-11-08)
Area: Gameplay actions (`QueryNearbyAction`, `GameplayHandler::QueryNearby`, `src/Context/NearbyListing.*`,
`src/Context/NameSearch.*`)
Source: designer's play-testing, 2026-11-07
Related: `archive/07-query-nearby-priority.md`, `archive/35-quest-and-object-query-fixes.md`

## Problem
With more than 20 objects in range, `query_nearby` said "Showing 20 of 38, most important first. Move or query again
for the rest." Querying again showed the same 20, because nothing could reach the rest: the action had no parameters,
and objects 21-38 got no id at all. The designer also wants an optional free-text search for one specific thing,
tolerant of typos.

## Result (2026-11-07)
- `query_nearby` takes two optional parameters. `page` (integer, at least 1, default 1) shows 20 objects per page.
  `search` (string, 1-64 characters) lists only the objects whose name or kind matches, best match first. Both can be
  combined. No data or `{}` behaves as before. Unknown fields, page 0, a blank or non-string search are refused with a
  reason (`Nearby::ParseQuery`).
- Every ranked object in range now gets an id, not only the 20 shown. Ids still follow the importance ranking, so the
  same surroundings give the same ids on every page and in every search, and an id seen on page 2 or in a search result
  works with `move_to_object`, `interact_with_object` and `attack`.
- Search (`Search::MatchScore`, engine-free): case, punctuation and word order are ignored, and every query word must
  match. Match quality runs: exact name, then a whole word ("crate" in "Ammo Crate"), then part of a word from 3
  letters on ("box" in "Mailbox", "moj" for "Mojave"), then a small typo (1 edit from 4 letters, 2 from 7, also against
  the start of a longer word). The category counts as a word too, so "door" or "container" list all of those. Within
  the same match quality the importance order is kept.
- The note under the listing is state only, following the 2026-11-07 rule that context does not explain actions:
  "Showing 21-38 of 38 (page 2 of 2), most important first.", "Search "box": 3 matches, best match first.", "Page 3 does
  not exist: 38 objects fill 2 pages.", "No nearby object matches "xyz".". The action description says how to page and
  search. The old `Objects::TruncationNote` with its "query again" text is gone.
- The scan radius is unchanged (2500 units, about 36 m), so search only finds what `query_nearby` could already see.
- Host tests: `tests/NameSearchTests.cpp`, `tests/NearbyListingTests.cpp` (282 pass). The Debug DLL builds. Not run in
  game.

Unverified: how Neuro uses the new parameters (whether she pages on her own when the note shows more pages), and
whether the typo tolerance is too loose or too strict for real object names.

## Manual in-game checks
1. Goodsprings (38 objects or so): `query_nearby` shows 1-20 with "Showing 1-20 of 38 (page 1 of 2)". With `page: 2`
   it shows 21-38 with ids 21-38. `page: 3` answers "Page 3 does not exist: 38 objects fill 2 pages.".
2. `move_to_object` with an id from page 2 walks to the right object.
3. `search: "mailbox"`, `"mail box"`, `"mailbx"`: the mailbox, with the same id it has in the plain listing.
4. `search: "crate"`: every crate, exact "Crate" before "Ammo Crate". `search: "container"`: every container.
5. `search: "deathclaw"` where there is none: "No nearby object matches "deathclaw"."
6. `{"page": 0}` or `{"radius": 5}`: refused with a message naming the field.

## Done when
Neuro can reach every object in range through pages or search, ids stay consistent between them, and the checks above
pass in game.

## Verified in game (designer, 2026-11-08)
Search works in game. The designer archived it without running the paging edge cases separately.
